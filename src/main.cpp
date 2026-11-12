#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <atomic>
#include <vector>
#include <chrono>

#include "windivert.h"
#include "ConfigManager.hpp"
#include "ProtocolParser.hpp"
#include "DesyncEngine.hpp"
#include "ServiceController.hpp"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")

#define SERVICE_NAME         "KillFuckDPI"
#define SERVICE_DISPLAY_NAME "KILL F#CK DPI Service"

static std::atomic<bool> g_running(true);
static HANDLE g_divert_handle = INVALID_HANDLE_VALUE;

// Статистика
static std::atomic<uint64_t> g_stat_tls(0);
static std::atomic<uint64_t> g_stat_quic(0);

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
        std::cout << "\n[!] Завершение работы KILL F#CK DPI и выгрузка перехватчика...\n";
        stop_divert();
        return TRUE;
    }
    return FALSE;
}

static void packet_loop(bool is_service) {
    const auto& cfg = kfdpi::ConfigManager::Instance().Get();
    std::string filter = kfdpi::ConfigManager::Instance().BuildFilter();

    g_divert_handle = WinDivertOpen(filter.c_str(), WINDIVERT_LAYER_NETWORK, 0, 0);
    if (g_divert_handle == INVALID_HANDLE_VALUE) {
        if (!is_service) {
            DWORD err = GetLastError();
            std::cerr << "[ОШИБКА] Не удалось инициализировать WinDivert (Код: " << err << ")\n";
            if (err == ERROR_FILE_NOT_FOUND) {
                std::cerr << "-> Файлы WinDivert64.sys или WinDivert.dll отсутствуют рядом с exe!\n";
            }
        }
        return;
    }

    // Тюнинг очередей драйвера для нулевых потерь и устойчивости
    WinDivertSetParam(g_divert_handle, WINDIVERT_PARAM_QUEUE_LENGTH, 8192);
    WinDivertSetParam(g_divert_handle, WINDIVERT_PARAM_QUEUE_TIME, 2000);
    WinDivertSetParam(g_divert_handle, WINDIVERT_PARAM_QUEUE_SIZE, 16 * 1024 * 1024);

    kfdpi::DesyncConfig dcfg;
    dcfg.strategy = cfg.disorder ? kfdpi::DesyncStrategy::Disorder : kfdpi::DesyncStrategy::Split;
    dcfg.split_pos = static_cast<size_t>(cfg.split_position);
    dcfg.drop_quic = cfg.drop_quic;

    kfdpi::DesyncEngine engine(dcfg);

    if (!is_service) {
        std::cout << "===================================================================\n";
        std::cout << "        KILL F#CK DPI - Модульное ядро (C++17 Engine)              \n";
        std::cout << "===================================================================\n";
        std::cout << " [*] YouTube 4K: QUIC Drop + TLS Split (ВКЛ)\n";
        std::cout << " [*] Discord (текст + голос): Активен\n";
        std::cout << " [*] Заблокированные сайты (Rutracker, Xplay.gg): Активен\n";
        std::cout << " [*] Игровой трафик (CS2 / FACEIT / Xplay UDP): Аппаратный NDIS Bypass (0ms)\n";
        std::cout << "-------------------------------------------------------------------\n";
        std::cout << " [i] Работает в фоне. Для остановки нажмите Ctrl + C.\n";
        std::cout << "===================================================================\n\n";
    }

    std::vector<uint8_t> packet_buf(WINDIVERT_MTU_MAX);
    WINDIVERT_ADDRESS addr;
    UINT packet_len = 0;
    std::vector<kfdpi::PacketFragment> fragments;
    auto last_stats_print = std::chrono::steady_clock::now();

    while (g_running) {
        if (!WinDivertRecv(g_divert_handle, packet_buf.data(), static_cast<UINT>(packet_buf.size()), &packet_len, &addr)) {
            if (!g_running) break;
            DWORD err = GetLastError();
            if (err == ERROR_INVALID_HANDLE || err == ERROR_OPERATION_ABORTED) break;
            continue;
        }

        kfdpi::ParsedPacket parsed;
        if (!kfdpi::ProtocolParser::Parse(packet_buf.data(), packet_len, parsed)) {
            WinDivertSend(g_divert_handle, packet_buf.data(), packet_len, nullptr, &addr);
            continue;
        }

        bool send_allowed = engine.Process(packet_buf.data(), packet_len, addr, parsed, fragments);

        if (!send_allowed) {
            // Пакет дропнут (например, QUIC)
            g_stat_quic++;
            continue;
        }

        if (fragments.size() > 1) {
            g_stat_tls++;
        }

        // Отправка фрагментов
        for (const auto& frag : fragments) {
            WinDivertSend(g_divert_handle, frag.buffer.data(), static_cast<UINT>(frag.buffer.size()), nullptr, &frag.address);
        }

        if (!is_service) {
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_stats_print).count() >= 1000) {
                last_stats_print = now;
                std::cout << "\r[*] Статус: TLS десинхронизировано: " << g_stat_tls
                          << " | QUIC сброшено: " << g_stat_quic << "   " << std::flush;
            }
        }
    }

    stop_divert();
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

    kfdpi::ConfigManager::Instance().Load("config.ini");

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--service") {
            return kfdpi::ServiceController::RunAsService(SERVICE_NAME, []() {
                packet_loop(true);
            }, []() {
                stop_divert();
            });
        } else if (arg == "--install") {
            if (!kfdpi::ServiceController::IsAdmin()) {
                if (kfdpi::ServiceController::Elevate(argc, argv)) return 0;
                std::cerr << "[ОШИБКА] Требуются права Администратора!\n";
                return 1;
            }
            char current_exe[MAX_PATH];
            GetModuleFileNameA(NULL, current_exe, MAX_PATH);
            kfdpi::ServiceController::Uninstall(SERVICE_NAME);
            if (kfdpi::ServiceController::Install(SERVICE_NAME, SERVICE_DISPLAY_NAME, current_exe)) {
                std::cout << "[OK] Служба KILL F#CK DPI успешно установлена.\n";
                kfdpi::ServiceController::Start(SERVICE_NAME);
                return 0;
            }
            std::cerr << "[ОШИБКА] Не удалось создать службу.\n";
            return 1;
        } else if (arg == "--uninstall") {
            if (!kfdpi::ServiceController::IsAdmin()) {
                if (kfdpi::ServiceController::Elevate(argc, argv)) return 0;
                std::cerr << "[ОШИБКА] Требуются права Администратора!\n";
                return 1;
            }
            if (kfdpi::ServiceController::Uninstall(SERVICE_NAME)) {
                std::cout << "[OK] Служба успешно остановлена и удалена.\n";
                return 0;
            }
            std::cerr << "[ОШИБКА] Не удалось удалить службу.\n";
            return 1;
        } else if (arg == "--start") {
            return kfdpi::ServiceController::Start(SERVICE_NAME) ? 0 : 1;
        } else if (arg == "--stop") {
            return kfdpi::ServiceController::Stop(SERVICE_NAME) ? 0 : 1;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "KILL F#CK DPI - Модульная C++ утилита обхода ТСПУ\n\n";
            std::cout << "Команды управления службой:\n";
            std::cout << "  --install         Установить службу с автозапуском\n";
            std::cout << "  --uninstall       Остановить и удалить службу\n";
            std::cout << "  --start           Запустить службу\n";
            std::cout << "  --stop            Остановить службу\n";
            return 0;
        }
    }

    if (!kfdpi::ServiceController::IsAdmin()) {
        if (kfdpi::ServiceController::Elevate(argc, argv)) {
            return 0;
        }
        std::cerr << "[КРИТИЧЕСКАЯ ОШИБКА] Требуются права Администратора для WinDivert!\n";
        system("pause");
        return 1;
    }

    SetConsoleCtrlHandler(console_handler, TRUE);
    packet_loop(false);
    return 0;
}
