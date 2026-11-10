#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <atomic>
#include <algorithm>
#include <cstdint>
#include <csignal>

#include "windivert.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "advapi32.lib")

// Глобальные переменные для корректной остановки сервиса
static std::atomic<bool> g_running(true);
static HANDLE g_handle = INVALID_HANDLE_VALUE;

// Структура конфигурации
struct Config {
    int split_position = 2;
    std::vector<uint16_t> target_ports = { 80, 443 };
    bool isolate_game_udp = true;
};

// Вспомогательная функция для обрезки пробелов
static inline std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

// Загрузка конфигурационного INI-файла
static bool load_config(const std::string& path, Config& cfg) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }

        size_t delimiter_pos = line.find('=');
        if (delimiter_pos == std::string::npos) {
            continue;
        }

        std::string key = trim(line.substr(0, delimiter_pos));
        std::string value = trim(line.substr(delimiter_pos + 1));

        if (key == "split_position") {
            try {
                int pos = std::stoi(value);
                if (pos > 0) {
                    cfg.split_position = pos;
                }
            } catch (...) {}
        } else if (key == "target_ports") {
            std::stringstream ss(value);
            std::string port_str;
            std::vector<uint16_t> ports;
            while (std::getline(ss, port_str, ',')) {
                try {
                    int p = std::stoi(trim(port_str));
                    if (p > 0 && p <= 65535) {
                        ports.push_back(static_cast<uint16_t>(p));
                    }
                } catch (...) {}
            }
            if (!ports.empty()) {
                cfg.target_ports = ports;
            }
        } else if (key == "isolate_game_udp") {
            std::string lower = value;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            cfg.isolate_game_udp = (lower == "true" || lower == "1" || lower == "yes");
        }
    }

    return true;
}

// Построение строкового фильтра WinDivert на основе целевых портов
static std::string build_filter_string(const std::vector<uint16_t>& ports) {
    std::ostringstream ss;
    // Строго перехватываем ТОЛЬКО исходящие TCP пакеты с непустым payload на указанные порты.
    // Любой UDP трафик аппаратно игнорируется фильтром ядра WFP.
    ss << "outbound and tcp and (";
    for (size_t i = 0; i < ports.size(); ++i) {
        if (i > 0) ss << " or ";
        ss << "tcp.DstPort == " << ports[i];
    }
    ss << ") and tcp.PayloadLength > 0";
    return ss.str();
}

// Обработчик системных сигналов завершения (Ctrl+C, закрытие консоли)
static BOOL WINAPI console_ctrl_handler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT || signal == CTRL_CLOSE_EVENT) {
        std::cout << "\n[!] Получен сигнал остановки. Корректное завершение и выгрузка драйвера...\n";
        g_running = false;
        if (g_handle != INVALID_HANDLE_VALUE) {
            WinDivertShutdown(g_handle, WINDIVERT_SHUTDOWN_BOTH);
            WinDivertClose(g_handle);
            g_handle = INVALID_HANDLE_VALUE;
        }
        return TRUE;
    }
    return FALSE;
}

// Проверка наличия прав Администратора
static bool is_running_as_admin() {
    BOOL is_admin = FALSE;
    PSID admin_group = NULL;
    SID_IDENTIFIER_AUTHORITY nt_auth = SECURITY_NT_AUTHORITY;

    if (AllocateAndInitializeSid(&nt_auth, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &admin_group)) {
        CheckTokenMembership(NULL, admin_group, &is_admin);
        FreeSid(admin_group);
    }
    return is_admin == TRUE;
}

static void print_banner(const Config& cfg, const std::string& filter) {
    std::cout << "===================================================================\n";
    std::cout << "        KILL F#CK DPI - Локальный обход блокировок ТСПУ           \n";
    std::cout << "===================================================================\n";
    std::cout << " [*] Режим фрагментации TLS : " << cfg.split_position << " байт(а)\n";
    std::cout << " [*] Целевые TCP порты     : ";
    for (size_t i = 0; i < cfg.target_ports.size(); ++i) {
        std::cout << cfg.target_ports[i] << (i + 1 < cfg.target_ports.size() ? ", " : "\n");
    }
    std::cout << " [*] Изоляция игрового UDP  : " << (cfg.isolate_game_udp ? "АКТИВНА (CS2/FACEIT 100% безопасны)" : "ВЫКЛ") << "\n";
    std::cout << " [*] Фильтр ядра WinDivert  : " << filter << "\n";
    std::cout << "-------------------------------------------------------------------\n";
    std::cout << " [i] Трафик перехватывается строго на прикладном TCP уровне.\n";
    std::cout << " [i] UDP пакеты не затрагиваются драйвером и идут без задержек.\n";
    std::cout << " [i] Для остановки нажмите Ctrl + C.\n";
    std::cout << "===================================================================\n\n";
}

int main(int argc, char* argv[]) {
    // Устанавливаем кодовую страницу UTF-8 для корректного вывода на русском языке
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    // Разбор аргументов справки до проверки прав
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            std::cout << "===================================================================\n";
            std::cout << "        KILL F#CK DPI - Справка по использованию\n";
            std::cout << "===================================================================\n";
            std::cout << "Использование: dpi_bypass.exe [ОПЦИИ]\n\n";
            std::cout << "Опции:\n";
            std::cout << "  --split-pos <N>   Позиция разделения ClientHello в байтах (по умолчанию: 2)\n";
            std::cout << "  --ports <список>  Целевые TCP порты через запятую (например: 80,443)\n";
            std::cout << "  --config <путь>   Путь к конфигурационному файлу INI (по умолчанию: config.ini)\n";
            std::cout << "  --help, -h        Показать эту справку\n\n";
            std::cout << "Примеры:\n";
            std::cout << "  dpi_bypass.exe --split-pos 2\n";
            std::cout << "  dpi_bypass.exe --split-pos 1 --ports 80,443,8443\n";
            std::cout << "  dpi_bypass.exe --config my_config.ini\n";
            std::cout << "===================================================================\n";
            return 0;
        }
    }

    if (!is_running_as_admin()) {
        std::cerr << "[КРИТИЧЕСКАЯ ОШИБКА] Утилита требует прав Администратора для загрузки драйвера WinDivert!\n";
        std::cerr << "Пожалуйста, запустите консоль или start.bat от имени Администратора.\n";
        return 1;
    }

    Config cfg;
    std::string config_path = "config.ini";

    // Попытка загрузить конфигурационный файл по умолчанию
    if (load_config(config_path, cfg)) {
        std::cout << "[+] Конфигурация загружена из " << config_path << "\n";
    }

    // Разбор остальных аргументов командной строки
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            config_path = argv[++i];
            if (load_config(config_path, cfg)) {
                std::cout << "[+] Конфигурация переопределена из " << config_path << "\n";
            } else {
                std::cerr << "[-] Не удалось открыть файл конфигурации: " << config_path << "\n";
            }
        } else if (arg == "--split-pos" && i + 1 < argc) {
            try {
                int pos = std::stoi(argv[++i]);
                if (pos > 0) cfg.split_position = pos;
            } catch (...) {
                std::cerr << "[-] Некорректное значение --split-pos\n";
            }
        } else if (arg == "--ports" && i + 1 < argc) {
            std::stringstream ss(argv[++i]);
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

    // Регистрация обработчика Ctrl+C
    if (!SetConsoleCtrlHandler(console_ctrl_handler, TRUE)) {
        std::cerr << "[-] Предупреждение: Не удалось зарегистрировать обработчик сигналов.\n";
    }

    std::string filter = build_filter_string(cfg.target_ports);
    print_banner(cfg, filter);

    // Открытие WinDivert перехватчика
    g_handle = WinDivertOpen(filter.c_str(), WINDIVERT_LAYER_NETWORK, 0, 0);
    if (g_handle == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        std::cerr << "[ОШИБКА] Не удалось инициализировать WinDivert (Код: " << err << ")\n";
        if (err == ERROR_FILE_NOT_FOUND) {
            std::cerr << "-> Проверьте наличие WinDivert.dll и WinDivert64.sys в рабочей папке!\n";
        } else if (err == ERROR_ACCESS_DENIED) {
            std::cerr << "-> Отказано в доступе. Запустите приложение с правами Администратора!\n";
        }
        return 1;
    }

    // Оптимизация параметров очереди пакетов в драйвере
    WinDivertSetParam(g_handle, WINDIVERT_PARAM_QUEUE_LENGTH, 8192);
    WinDivertSetParam(g_handle, WINDIVERT_PARAM_QUEUE_TIME, 2000);
    WinDivertSetParam(g_handle, WINDIVERT_PARAM_QUEUE_SIZE, 8 * 1024 * 1024);

    std::cout << "[+] Драйвер WinDivert успешно запущен и перехватывает трафик.\n";
    std::cout << "[*] Ожидание TLS ClientHello пакетов...\n\n";

    // Буферы для обработки пакетов (выделены в куче во избежание переполнения стека)
    std::vector<uint8_t> packet_buf(WINDIVERT_MTU_MAX);
    std::vector<uint8_t> seg1_buf(WINDIVERT_MTU_MAX);
    std::vector<uint8_t> seg2_buf(WINDIVERT_MTU_MAX);

    WINDIVERT_ADDRESS addr;
    UINT packet_len = 0;
    uint64_t processed_tls_count = 0;
    uint64_t passthrough_count = 0;

    while (g_running) {
        if (!WinDivertRecv(g_handle, packet_buf.data(), static_cast<UINT>(packet_buf.size()), &packet_len, &addr)) {
            if (!g_running) break;
            DWORD err = GetLastError();
            if (err == ERROR_INVALID_HANDLE || err == ERROR_OPERATION_ABORTED) {
                break;
            }
            continue;
        }

        PWINDIVERT_IPHDR ip_hdr = nullptr;
        PWINDIVERT_IPV6HDR ip6_hdr = nullptr;
        PWINDIVERT_TCPHDR tcp_hdr = nullptr;
        PVOID payload = nullptr;
        UINT payload_len = 0;

        // Разбор заголовков входящего пакета
        if (!WinDivertHelperParsePacket(
                packet_buf.data(), packet_len,
                &ip_hdr, &ip6_hdr, nullptr,
                nullptr, nullptr,
                &tcp_hdr, nullptr,
                &payload, &payload_len,
                nullptr, nullptr) || payload == nullptr || payload_len == 0 || tcp_hdr == nullptr) {
            // Если пакет не содержит полезной нагрузки или не удалось разобрать - пропускаем без изменений
            WinDivertSend(g_handle, packet_buf.data(), packet_len, nullptr, &addr);
            passthrough_count++;
            continue;
        }

        const uint8_t* p = static_cast<const uint8_t*>(payload);

        // Детектирование TLS ClientHello:
        // Байт 0: 0x16 (ContentType = Handshake)
        // Байт 1: 0x03 (TLS Record Version major: SSL 3.0 / TLS 1.0-1.3)
        // Байт 5: 0x01 (HandshakeType = ClientHello)
        bool is_tls_client_hello = (payload_len >= 6 && p[0] == 0x16 && p[1] == 0x03 && p[5] == 0x01);

        if (!is_tls_client_hello) {
            // Обычный TCP трафик (HTTP GET/POST, TLS Application Data, ACK) передается немедленно
            WinDivertSend(g_handle, packet_buf.data(), packet_len, nullptr, &addr);
            passthrough_count++;
            continue;
        }

        // Проверяем возможность фрагментации
        int split_pos = cfg.split_position;
        if (split_pos <= 0 || static_cast<UINT>(split_pos) >= payload_len) {
            // Позиция разделения вне границ полезной нагрузки - отправляем целиком
            WinDivertSend(g_handle, packet_buf.data(), packet_len, nullptr, &addr);
            continue;
        }

        // Длина всех заголовков до payload (IP + TCP опции)
        size_t header_len = static_cast<uint8_t*>(payload) - packet_buf.data();
        size_t seg1_payload_len = static_cast<size_t>(split_pos);
        size_t seg2_payload_len = static_cast<size_t>(payload_len) - seg1_payload_len;

        size_t seg1_total_len = header_len + seg1_payload_len;
        size_t seg2_total_len = header_len + seg2_payload_len;

        if (seg1_total_len > seg1_buf.size() || seg2_total_len > seg2_buf.size()) {
            WinDivertSend(g_handle, packet_buf.data(), packet_len, nullptr, &addr);
            continue;
        }

        // =================================================================
        // Формирование Сегмента 1 (первые split_pos байт ClientHello)
        // =================================================================
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
        } else if (ip6_hdr1) {
            ip6_hdr1->Length = htons(static_cast<uint16_t>(seg1_total_len - sizeof(WINDIVERT_IPV6HDR)));
        }

        if (tcp_hdr1) {
            tcp_hdr1->Psh = 0; // Снимаем PSH для первого фрагмента
        }

        WINDIVERT_ADDRESS addr1 = addr;
        WinDivertHelperCalcChecksums(seg1_buf.data(), static_cast<UINT>(seg1_total_len), &addr1, 0);
        WinDivertSend(g_handle, seg1_buf.data(), static_cast<UINT>(seg1_total_len), nullptr, &addr1);

        // =================================================================
        // Формирование Сегмента 2 (оставшаяся часть ClientHello с SNI)
        // =================================================================
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
            if (ip_hdr) {
                // Инкрементируем IP ID для второго сегмента
                ip_hdr2->Id = htons(ntohs(ip_hdr->Id) + 1);
            }
        } else if (ip6_hdr2) {
            ip6_hdr2->Length = htons(static_cast<uint16_t>(seg2_total_len - sizeof(WINDIVERT_IPV6HDR)));
        }

        if (tcp_hdr2) {
            // Смещаем TCP Sequence Number на размер первого фрагмента
            uint32_t original_seq = ntohl(tcp_hdr->SeqNum);
            tcp_hdr2->SeqNum = htonl(original_seq + static_cast<uint32_t>(seg1_payload_len));
            tcp_hdr2->Psh = tcp_hdr->Psh;
        }

        WINDIVERT_ADDRESS addr2 = addr;
        WinDivertHelperCalcChecksums(seg2_buf.data(), static_cast<UINT>(seg2_total_len), &addr2, 0);
        WinDivertSend(g_handle, seg2_buf.data(), static_cast<UINT>(seg2_total_len), nullptr, &addr2);

        processed_tls_count++;
        std::cout << "[+] Обход DPI активирован: TLS ClientHello (" << payload_len 
                  << " байт) успешно разделен на " << seg1_payload_len << " + " 
                  << seg2_payload_len << " байт [Всего обойдено: " << processed_tls_count << "]\n";
    }

    std::cout << "\n[+] Остановка службы. Обработано TLS рукопожатий: " << processed_tls_count << "\n";
    if (g_handle != INVALID_HANDLE_VALUE) {
        WinDivertClose(g_handle);
        g_handle = INVALID_HANDLE_VALUE;
    }
    std::cout << "[+] Драйвер WinDivert успешно выгружен.\n";
    return 0;
}
