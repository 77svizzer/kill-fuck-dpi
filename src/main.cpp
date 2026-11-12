#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <shellapi.h>
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <atomic>
#include <algorithm>
#include <cstdint>
#include <chrono>

#include "windivert.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")

#define SERVICE_NAME        "KillFuckDPI"
#define SERVICE_DISPLAY_NAME "KILL F#CK DPI Service"

struct AppConfig {
    int split_position = 2;
    bool disorder = false;      // По умолчанию false: строгий in-order split (Seg1 затем Seg2) - 100% совместимость с TCP
    bool drop_quic = true;      // Дроп UDP 443 для мгновенного перехода YouTube на HTTP/2
    std::vector<uint16_t> target_ports = { 80, 443 };
    std::string config_file = "config.ini";
    bool run_as_service = false;
};

static AppConfig g_cfg;
static std::atomic<bool> g_running(true);
static HANDLE g_divert_handle = INVALID_HANDLE_VALUE;
static SERVICE_STATUS g_service_status;
static SERVICE_STATUS_HANDLE g_status_handle = NULL;

// Статистика
static std::atomic<uint64_t> g_tls_split_count(0);
static std::atomic<uint64_t> g_quic_drop_count(0);

static inline std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

// Загрузка настроек из config.ini
static bool load_config(const std::string& path, AppConfig& cfg) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;

        size_t delimiter_pos = line.find('=');
        if (delimiter_pos == std::string::npos) continue;

        std::string key = trim(line.substr(0, delimiter_pos));
        std::string value = trim(line.substr(delimiter_pos + 1));

        if (key == "split_position") {
            try { cfg.split_position = std::stoi(value); } catch (...) {}
        } else if (key == "disorder") {
            std::string lower = value;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            cfg.disorder = (lower == "true" || lower == "1" || lower == "yes");
        } else if (key == "drop_quic") {
            std::string lower = value;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            cfg.drop_quic = (lower == "true" || lower == "1" || lower == "yes");
        } else if (key == "target_ports") {
            std::stringstream ss(value);
            std::string port_str;
            std::vector<uint16_t> ports;
            while (std::getline(ss, port_str, ',')) {
                try {
                    int p = std::stoi(trim(port_str));
                    if (p > 0 && p <= 65535) ports.push_back(static_cast<uint16_t>(p));
                } catch (...) {}
            }
            if (!ports.empty()) cfg.target_ports = ports;
        }
    }
    return true;
}

// Построение фильтра WinDivert
static std::string build_filter(const AppConfig& cfg) {
    std::ostringstream ss;
    ss << "outbound and (";
    // TCP порты (80, 443) с полезной нагрузкой
    ss << "(tcp and (";
    for (size_t i = 0; i < cfg.target_ports.size(); ++i) {
        if (i > 0) ss << " or ";
        ss << "tcp.DstPort == " << cfg.target_ports[i];
    }
    ss << ") and tcp.PayloadLength > 0)";

    // UDP 443 (QUIC drop для мгновенного перехода YouTube на HTTP/2)
    if (cfg.drop_quic) {
        ss << " or (udp and udp.DstPort == 443)";
    }

    // ВАЖНО: Игровой трафик UDP (CS2, Xplay, FACEIT) НЕ перехватывается,
    // он идёт напрямую через нативный сетевой стек ядра без каких-либо задержек и искажений!

    ss << ")";
    return ss.str();
}

// Проверка прав Администратора
static bool is_admin() {
    BOOL is_adm = FALSE;
    PSID admin_group = NULL;
    SID_IDENTIFIER_AUTHORITY nt_auth = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&nt_auth, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &admin_group)) {
        CheckTokenMembership(NULL, admin_group, &is_adm);
        FreeSid(admin_group);
    }
    return is_adm == TRUE;
}

// Автоповышение прав (UAC) при ручном даблклике
static bool elevate_process(int argc, char* argv[]) {
    char szPath[MAX_PATH];
    if (!GetModuleFileNameA(NULL, szPath, MAX_PATH)) return false;

    std::string args;
    for (int i = 1; i < argc; ++i) {
        if (i > 1) args += " ";
        args += argv[i];
    }

    SHELLEXECUTEINFOA sei = { sizeof(sei) };
    sei.lpVerb = "runas";
    sei.lpFile = szPath;
    sei.lpParameters = args.c_str();
    sei.nShow = SW_NORMAL;

    return ShellExecuteExA(&sei) == TRUE;
}

static void stop_divert() {
    g_running = false;
    if (g_divert_handle != INVALID_HANDLE_VALUE) {
        WinDivertShutdown(g_divert_handle, WINDIVERT_SHUTDOWN_BOTH);
        WinDivertClose(g_divert_handle);
        g_divert_handle = INVALID_HANDLE_VALUE;
    }
}

static BOOL WINAPI console_handler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT || signal == CTRL_CLOSE_EVENT) {
        std::cout << "\n[!] Остановка службы KILL F#CK DPI и выгрузка драйвера...\n";
        stop_divert();
        return TRUE;
    }
    return FALSE;
}

// Главный цикл перехвата и десинхронизации пакетов
static void packet_loop() {
    std::string filter = build_filter(g_cfg);

    g_divert_handle = WinDivertOpen(filter.c_str(), WINDIVERT_LAYER_NETWORK, 0, 0);
    if (g_divert_handle == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (!g_cfg.run_as_service) {
            std::cerr << "[ОШИБКА] Не удалось инициализировать WinDivert (Код ошибки: " << err << ")\n";
            if (err == ERROR_FILE_NOT_FOUND) {
                std::cerr << "-> WinDivert64.sys или WinDivert.dll не найдены рядом с программой!\n";
            }
        }
        return;
    }

    WinDivertSetParam(g_divert_handle, WINDIVERT_PARAM_QUEUE_LENGTH, 8192);
    WinDivertSetParam(g_divert_handle, WINDIVERT_PARAM_QUEUE_TIME, 2000);
    WinDivertSetParam(g_divert_handle, WINDIVERT_PARAM_QUEUE_SIZE, 8 * 1024 * 1024);

    if (!g_cfg.run_as_service) {
        std::cout << "===================================================================\n";
        std::cout << "        KILL F#CK DPI - Активен (Native C++ Engine)                \n";
        std::cout << "===================================================================\n";
        std::cout << " [*] YouTube 4K: QUIC Drop + TLS Split (ВКЛ)\n";
        std::cout << " [*] Discord (текст + голосовые каналы): Активен\n";
        std::cout << " [*] Заблокированные сайты (Rutracker, Xplay.gg): Активен\n";
        std::cout << " [*] Игровой трафик (CS2 / FACEIT / Xplay UDP): Нативный стек ядра (0ms задержки)\n";
        std::cout << "-------------------------------------------------------------------\n";
        std::cout << " [i] Работает в фоне. Для остановки нажмите Ctrl + C.\n";
        std::cout << "===================================================================\n\n";
    }

    std::vector<uint8_t> packet_buf(WINDIVERT_MTU_MAX);
    std::vector<uint8_t> seg1_buf(WINDIVERT_MTU_MAX);
    std::vector<uint8_t> seg2_buf(WINDIVERT_MTU_MAX);

    WINDIVERT_ADDRESS addr;
    UINT packet_len = 0;
    auto last_status_time = std::chrono::steady_clock::now();

    while (g_running) {
        if (!WinDivertRecv(g_divert_handle, packet_buf.data(), static_cast<UINT>(packet_buf.size()), &packet_len, &addr)) {
            if (!g_running) break;
            DWORD err = GetLastError();
            if (err == ERROR_INVALID_HANDLE || err == ERROR_OPERATION_ABORTED) break;
            continue;
        }

        PWINDIVERT_IPHDR ip_hdr = nullptr;
        PWINDIVERT_IPV6HDR ip6_hdr = nullptr;
        PWINDIVERT_TCPHDR tcp_hdr = nullptr;
        PWINDIVERT_UDPHDR udp_hdr = nullptr;
        PVOID payload = nullptr;
        UINT payload_len = 0;

        if (!WinDivertHelperParsePacket(
                packet_buf.data(), packet_len,
                &ip_hdr, &ip6_hdr, nullptr,
                nullptr, nullptr,
                &tcp_hdr, &udp_hdr,
                &payload, &payload_len,
                nullptr, nullptr)) {
            WinDivertSend(g_divert_handle, packet_buf.data(), packet_len, nullptr, &addr);
            continue;
        }

        // =================================================================
        // 1. UDP: ТОЛЬКО порт 443 (QUIC drop)
        // =================================================================
        if (udp_hdr != nullptr) {
            uint16_t dst_port = ntohs(udp_hdr->DstPort);
            if (dst_port == 443 && g_cfg.drop_quic) {
                // Дропаем исходящий QUIC -> браузер мгновенно переходит на HTTP/2 (TCP 443)
                g_quic_drop_count++;
                continue;
            }
            // Прочие UDP пакеты отправляем без изменений
            WinDivertSend(g_divert_handle, packet_buf.data(), packet_len, nullptr, &addr);
            continue;
        }

        // =================================================================
        // 2. TCP: Десинхронизация TLS ClientHello
        // =================================================================
        if (tcp_hdr == nullptr || payload == nullptr || payload_len == 0) {
            WinDivertSend(g_divert_handle, packet_buf.data(), packet_len, nullptr, &addr);
            continue;
        }

        const uint8_t* p = static_cast<const uint8_t*>(payload);
        // Сигнатура TLS ClientHello:
        // Record Type 0x16 (Handshake), Version 0x03 0x01/02/03, Handshake Type 0x01 (ClientHello)
        bool is_tls_client_hello = (payload_len >= 6 && p[0] == 0x16 && p[1] == 0x03 && p[5] == 0x01);

        if (!is_tls_client_hello) {
            WinDivertSend(g_divert_handle, packet_buf.data(), packet_len, nullptr, &addr);
            continue;
        }

        int split_pos = g_cfg.split_position;
        if (split_pos <= 0 || static_cast<UINT>(split_pos) >= payload_len) {
            WinDivertSend(g_divert_handle, packet_buf.data(), packet_len, nullptr, &addr);
            continue;
        }

        size_t header_len = static_cast<uint8_t*>(payload) - packet_buf.data();
        size_t seg1_payload_len = static_cast<size_t>(split_pos);
        size_t seg2_payload_len = static_cast<size_t>(payload_len) - seg1_payload_len;

        size_t seg1_total_len = header_len + seg1_payload_len;
        size_t seg2_total_len = header_len + seg2_payload_len;

        if (seg1_total_len > seg1_buf.size() || seg2_total_len > seg2_buf.size()) {
            WinDivertSend(g_divert_handle, packet_buf.data(), packet_len, nullptr, &addr);
            continue;
        }

        // --- ПОДГОТОВКА СЕГМЕНТА 1 ---
        memcpy(seg1_buf.data(), packet_buf.data(), header_len);
        memcpy(seg1_buf.data() + header_len, payload, seg1_payload_len);

        PWINDIVERT_IPHDR ip_hdr1 = nullptr;
        PWINDIVERT_IPV6HDR ip6_hdr1 = nullptr;
        PWINDIVERT_TCPHDR tcp_hdr1 = nullptr;
        WinDivertHelperParsePacket(seg1_buf.data(), static_cast<UINT>(seg1_total_len),
                                  &ip_hdr1, &ip6_hdr1, nullptr, nullptr, nullptr,
                                  &tcp_hdr1, nullptr, nullptr, nullptr, nullptr, nullptr);

        if (ip_hdr1) {
            ip_hdr1->Length = htons(static_cast<uint16_t>(seg1_total_len));
            ip_hdr1->Checksum = 0;
        } else if (ip6_hdr1) {
            ip6_hdr1->Length = htons(static_cast<uint16_t>(seg1_total_len - sizeof(WINDIVERT_IPV6HDR)));
        }

        if (tcp_hdr1) {
            tcp_hdr1->Checksum = 0;
            tcp_hdr1->Psh = 0;
        }

        WINDIVERT_ADDRESS addr1 = addr;
        addr1.IPChecksum = 0;
        addr1.TCPChecksum = 0;
        addr1.UDPChecksum = 0;
        WinDivertHelperCalcChecksums(seg1_buf.data(), static_cast<UINT>(seg1_total_len), NULL, 0);

        // --- ПОДГОТОВКА СЕГМЕНТА 2 (с SNI) ---
        memcpy(seg2_buf.data(), packet_buf.data(), header_len);
        memcpy(seg2_buf.data() + header_len, static_cast<const uint8_t*>(payload) + seg1_payload_len, seg2_payload_len);

        PWINDIVERT_IPHDR ip_hdr2 = nullptr;
        PWINDIVERT_IPV6HDR ip6_hdr2 = nullptr;
        PWINDIVERT_TCPHDR tcp_hdr2 = nullptr;
        WinDivertHelperParsePacket(seg2_buf.data(), static_cast<UINT>(seg2_total_len),
                                  &ip_hdr2, &ip6_hdr2, nullptr, nullptr, nullptr,
                                  &tcp_hdr2, nullptr, nullptr, nullptr, nullptr, nullptr);

        if (ip_hdr2) {
            ip_hdr2->Length = htons(static_cast<uint16_t>(seg2_total_len));
            ip_hdr2->Checksum = 0;
            if (ip_hdr) ip_hdr2->Id = htons(ntohs(ip_hdr->Id) + 1);
        } else if (ip6_hdr2) {
            ip6_hdr2->Length = htons(static_cast<uint16_t>(seg2_total_len - sizeof(WINDIVERT_IPV6HDR)));
        }

        if (tcp_hdr2) {
            uint32_t original_seq = ntohl(tcp_hdr->SeqNum);
            tcp_hdr2->SeqNum = htonl(original_seq + static_cast<uint32_t>(seg1_payload_len));
            tcp_hdr2->Checksum = 0;
            tcp_hdr2->Psh = tcp_hdr->Psh;
        }

        WINDIVERT_ADDRESS addr2 = addr;
        addr2.IPChecksum = 0;
        addr2.TCPChecksum = 0;
        addr2.UDPChecksum = 0;
        WinDivertHelperCalcChecksums(seg2_buf.data(), static_cast<UINT>(seg2_total_len), NULL, 0);

        // Отправка в сеть
        if (g_cfg.disorder) {
            WinDivertSend(g_divert_handle, seg2_buf.data(), static_cast<UINT>(seg2_total_len), nullptr, &addr2);
            WinDivertSend(g_divert_handle, seg1_buf.data(), static_cast<UINT>(seg1_total_len), nullptr, &addr1);
        } else {
            WinDivertSend(g_divert_handle, seg1_buf.data(), static_cast<UINT>(seg1_total_len), nullptr, &addr1);
            WinDivertSend(g_divert_handle, seg2_buf.data(), static_cast<UINT>(seg2_total_len), nullptr, &addr2);
        }

        g_tls_split_count++;

        // Компактный вывод статистики в консоли
        if (!g_cfg.run_as_service) {
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_status_time).count() >= 1000) {
                last_status_time = now;
                std::cout << "\r[*] Статус: TLS ClientHello обойдено: " << g_tls_split_count
                          << " | QUIC сброшено: " << g_quic_drop_count << "   " << std::flush;
            }
        }
    }

    if (g_divert_handle != INVALID_HANDLE_VALUE) {
        WinDivertClose(g_divert_handle);
        g_divert_handle = INVALID_HANDLE_VALUE;
    }
}

// Служба Windows
static VOID WINAPI ServiceCtrlHandler(DWORD request) {
    switch (request) {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
            g_service_status.dwCurrentState = SERVICE_STOP_PENDING;
            SetServiceStatus(g_status_handle, &g_service_status);
            stop_divert();
            g_service_status.dwCurrentState = SERVICE_STOPPED;
            SetServiceStatus(g_status_handle, &g_service_status);
            break;
        default:
            break;
    }
}

static VOID WINAPI ServiceMain(DWORD, LPSTR*) {
    g_status_handle = RegisterServiceCtrlHandlerA(SERVICE_NAME, ServiceCtrlHandler);
    if (!g_status_handle) return;

    g_service_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_service_status.dwCurrentState = SERVICE_START_PENDING;
    g_service_status.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    g_service_status.dwWin32ExitCode = 0;
    g_service_status.dwServiceSpecificExitCode = 0;
    g_service_status.dwCheckPoint = 0;
    g_service_status.dwWaitHint = 0;
    SetServiceStatus(g_status_handle, &g_service_status);

    g_service_status.dwCurrentState = SERVICE_RUNNING;
    SetServiceStatus(g_status_handle, &g_service_status);

    packet_loop();

    g_service_status.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus(g_status_handle, &g_service_status);
}

static bool install_service() {
    char path[MAX_PATH];
    if (!GetModuleFileNameA(NULL, path, MAX_PATH)) return false;

    std::string bin_path = "\"";
    bin_path += path;
    bin_path += "\" --service";

    SC_HANDLE sc_manager = OpenSCManagerA(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!sc_manager) return false;

    SC_HANDLE service = CreateServiceA(
        sc_manager,
        SERVICE_NAME,
        SERVICE_DISPLAY_NAME,
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_AUTO_START,
        SERVICE_ERROR_NORMAL,
        bin_path.c_str(),
        NULL, NULL, NULL, NULL, NULL
    );

    if (!service) {
        CloseServiceHandle(sc_manager);
        return false;
    }

    SERVICE_DESCRIPTIONA desc;
    char desc_str[] = "Автономная C++ служба обхода DPI (YouTube 4K, Discord, сайты, CS2/Xplay/FACEIT)";
    desc.lpDescription = desc_str;
    ChangeServiceConfig2A(service, SERVICE_CONFIG_DESCRIPTION, &desc);

    CloseServiceHandle(service);
    CloseServiceHandle(sc_manager);
    return true;
}

static bool uninstall_service() {
    SC_HANDLE sc_manager = OpenSCManagerA(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!sc_manager) return false;

    SC_HANDLE service = OpenServiceA(sc_manager, SERVICE_NAME, SERVICE_STOP | DELETE);
    if (!service) {
        CloseServiceHandle(sc_manager);
        return false;
    }

    SERVICE_STATUS status;
    ControlService(service, SERVICE_CONTROL_STOP, &status);
    BOOL res = DeleteService(service);

    CloseServiceHandle(service);
    CloseServiceHandle(sc_manager);
    return res == TRUE;
}

static bool start_service_cmd() {
    SC_HANDLE sc_manager = OpenSCManagerA(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!sc_manager) return false;

    SC_HANDLE service = OpenServiceA(sc_manager, SERVICE_NAME, SERVICE_START);
    if (!service) {
        CloseServiceHandle(sc_manager);
        return false;
    }

    BOOL res = StartServiceA(service, 0, NULL);
    CloseServiceHandle(service);
    CloseServiceHandle(sc_manager);
    return res == TRUE;
}

static bool stop_service_cmd() {
    SC_HANDLE sc_manager = OpenSCManagerA(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!sc_manager) return false;

    SC_HANDLE service = OpenServiceA(sc_manager, SERVICE_NAME, SERVICE_STOP);
    if (!service) {
        CloseServiceHandle(sc_manager);
        return false;
    }

    SERVICE_STATUS status;
    BOOL res = ControlService(service, SERVICE_CONTROL_STOP, &status);
    CloseServiceHandle(service);
    CloseServiceHandle(sc_manager);
    return res == TRUE;
}

int main(int argc, char* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    // Устанавливаем рабочую директорию в папку с исполняемым файлом
    char exe_path[MAX_PATH];
    if (GetModuleFileNameA(NULL, exe_path, MAX_PATH)) {
        char* last_slash = strrchr(exe_path, '\\');
        if (last_slash) {
            *last_slash = '\0';
            SetCurrentDirectoryA(exe_path);
        }
    }

    // Обработка параметров командной строки
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--service") {
            g_cfg.run_as_service = true;
            load_config(g_cfg.config_file, g_cfg);
            SERVICE_TABLE_ENTRYA dispatch_table[] = {
                { (LPSTR)SERVICE_NAME, (LPSERVICE_MAIN_FUNCTIONA)ServiceMain },
                { NULL, NULL }
            };
            StartServiceCtrlDispatcherA(dispatch_table);
            return 0;
        } else if (arg == "--install") {
            if (!is_admin()) {
                if (elevate_process(argc, argv)) return 0;
                std::cerr << "[ОШИБКА] Требуются права Администратора!\n";
                return 1;
            }
            uninstall_service();
            if (install_service()) {
                std::cout << "[OK] Служба KILL F#CK DPI успешно установлена.\n";
                start_service_cmd();
                return 0;
            } else {
                std::cerr << "[ОШИБКА] Не удалось создать службу.\n";
                return 1;
            }
        } else if (arg == "--uninstall") {
            if (!is_admin()) {
                if (elevate_process(argc, argv)) return 0;
                std::cerr << "[ОШИБКА] Требуются права Администратора!\n";
                return 1;
            }
            if (uninstall_service()) {
                std::cout << "[OK] Служба успешно остановлена и удалена.\n";
                return 0;
            } else {
                std::cerr << "[ОШИБКА] Не удалось удалить службу.\n";
                return 1;
            }
        } else if (arg == "--start") {
            if (start_service_cmd()) {
                std::cout << "[OK] Служба запущена.\n";
                return 0;
            }
            return 1;
        } else if (arg == "--stop") {
            if (stop_service_cmd()) {
                std::cout << "[OK] Служба остановлена.\n";
                return 0;
            }
            return 1;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "===================================================================\n";
            std::cout << "               KILL F#CK DPI - Полностью локальная C++ утилита     \n";
            std::cout << "===================================================================\n";
            std::cout << "Использование: dpi_bypass.exe [ОПЦИИ]\n\n";
            std::cout << "Команды управления службой:\n";
            std::cout << "  --install         Установить утилиту как службу Windows (автозапуск)\n";
            std::cout << "  --uninstall       Остановить и удалить службу Windows\n";
            std::cout << "  --start           Запустить службу\n";
            std::cout << "  --stop            Остановить службу\n\n";
            std::cout << "Параметры запуска:\n";
            std::cout << "  --split-pos <N>   Позиция разделения TLS ClientHello (по умолчанию: 2)\n";
            std::cout << "  --no-disorder     Отключить инверсию сегментов\n";
            std::cout << "  --no-quic-drop    Отключить сброс QUIC\n";
            std::cout << "  --config <путь>   Путь к файлу конфигурации\n";
            return 0;
        }
    }

    // Если запущено пользователем без прав Администратора — автоповышение через UAC
    if (!is_admin()) {
        if (elevate_process(argc, argv)) {
            return 0;
        }
        std::cerr << "[КРИТИЧЕСКАЯ ОШИБКА] Требуются права Администратора!\n";
        system("pause");
        return 1;
    }

    load_config(g_cfg.config_file, g_cfg);

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--split-pos" && i + 1 < argc) {
            try { g_cfg.split_position = std::stoi(argv[++i]); } catch (...) {}
        } else if (arg == "--no-disorder") {
            g_cfg.disorder = false;
        } else if (arg == "--no-quic-drop") {
            g_cfg.drop_quic = false;
        } else if (arg == "--config" && i + 1 < argc) {
            g_cfg.config_file = argv[++i];
            load_config(g_cfg.config_file, g_cfg);
        }
    }

    SetConsoleCtrlHandler(console_handler, TRUE);

    // Запуск цикла перехвата пакетов
    packet_loop();

    return 0;
}
