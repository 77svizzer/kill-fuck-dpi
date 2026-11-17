#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <atomic>
#include <vector>
#include <chrono>
#include <thread>

#include "windivert.h"
#include "ConfigManager.hpp"
#include "ProtocolParser.hpp"
#include "DesyncEngine.hpp"
#include "HostlistManager.hpp"
#include "ServiceController.hpp"
#include "GuiManager.hpp"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib")

#define SERVICE_NAME         "KillFuckDPI"
#define SERVICE_DISPLAY_NAME "KILL F#CK DPI Service"

static std::atomic<bool> g_running(false);
static HANDLE g_divert_handle = INVALID_HANDLE_VALUE;
static std::thread g_worker_thread;

// Глобальная статистика пакетов
std::atomic<uint64_t> g_stat_tls(0);
std::atomic<uint64_t> g_stat_fake(0);
std::atomic<uint64_t> g_stat_quic(0);
std::atomic<uint64_t> g_stat_discord_voice(0);

void stop_divert();

static LONG WINAPI CrashHandler(EXCEPTION_POINTERS*) {
    stop_divert();
    return EXCEPTION_CONTINUE_SEARCH;
}

void stop_divert() {
    g_running = false;
    if (g_divert_handle != INVALID_HANDLE_VALUE) {
        WinDivertShutdown(g_divert_handle, WINDIVERT_SHUTDOWN_BOTH);
        WinDivertClose(g_divert_handle);
        g_divert_handle = INVALID_HANDLE_VALUE;
    }
    if (g_worker_thread.joinable() && std::this_thread::get_id() != g_worker_thread.get_id()) {
        g_worker_thread.join();
    }
    // Полная выгрузка драйвера WinDivert из ядра Windows,
    // чтобы интернет никогда не ломался при закрытии или перезагрузке
    kfdpi::ServiceController::ForceUnloadWinDivertKernelDriver();
}

bool is_divert_running() {
    return g_running.load() && (g_divert_handle != INVALID_HANDLE_VALUE);
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
    g_running = true;
    const auto& cfg = kfdpi::ConfigManager::Instance().Get();
    std::string filter = kfdpi::ConfigManager::Instance().BuildFilter();

    // Загрузка списков хостов
    kfdpi::HostlistManager::Instance().LoadLists(cfg.lists_dir);

    g_divert_handle = WinDivertOpen(filter.c_str(), WINDIVERT_LAYER_NETWORK, 0, 0);
    if (g_divert_handle == INVALID_HANDLE_VALUE) {
        g_running = false;
        if (!is_service) {
            DWORD err = GetLastError();
            std::cerr << "[ОШИБКА] Не удалось инициализировать WinDivert (Код: " << err << ")\n";
            if (err == ERROR_FILE_NOT_FOUND) {
                std::cerr << "-> Файлы WinDivert64.sys или WinDivert.dll отсутствуют рядом с exe!\n";
            }
        }
        return;
    }

    WinDivertSetParam(g_divert_handle, WINDIVERT_PARAM_QUEUE_LENGTH, 8192);
    WinDivertSetParam(g_divert_handle, WINDIVERT_PARAM_QUEUE_TIME, 2000);
    WinDivertSetParam(g_divert_handle, WINDIVERT_PARAM_QUEUE_SIZE, 16 * 1024 * 1024);

    kfdpi::DesyncConfig dcfg;

    // 1. Стратегия
    if (cfg.strategy == "fake_split2") {
        dcfg.strategy = kfdpi::DesyncStrategy::Fake_Split2;
    } else if (cfg.strategy == "fake_disorder") {
        dcfg.strategy = kfdpi::DesyncStrategy::Fake_Disorder;
    } else if (cfg.strategy == "disorder") {
        dcfg.strategy = kfdpi::DesyncStrategy::Disorder;
    } else {
        dcfg.strategy = kfdpi::DesyncStrategy::Split;
    }

    // 2. Позиция разделения
    if (cfg.split_position == "midsld") {
        dcfg.split_mode = kfdpi::SplitPositionMode::Midsld;
    } else if (cfg.split_position == "sni") {
        dcfg.split_mode = kfdpi::SplitPositionMode::Sni;
    } else {
        dcfg.split_mode = kfdpi::SplitPositionMode::Fixed;
        try {
            dcfg.split_pos = std::stoul(cfg.split_position);
        } catch (...) {
            dcfg.split_pos = 2;
        }
    }

    // 3. Fooling методы
    dcfg.fooling = kfdpi::FoolingMethod::None;
    if (cfg.fooling.find("badsum") != std::string::npos) {
        dcfg.fooling = dcfg.fooling | kfdpi::FoolingMethod::BadSum;
    }
    if (cfg.fooling.find("badseq") != std::string::npos) {
        dcfg.fooling = dcfg.fooling | kfdpi::FoolingMethod::BadSeq;
    }
    if (cfg.fooling.find("autottl") != std::string::npos) {
        dcfg.fooling = dcfg.fooling | kfdpi::FoolingMethod::AutoTTL;
    }

    dcfg.fake_ttl = static_cast<uint8_t>(cfg.fake_ttl);
    dcfg.drop_quic = cfg.drop_quic;
    dcfg.discord_voice_fake = cfg.discord_voice_fake;
    dcfg.use_hostlists = cfg.use_hostlists;

    kfdpi::DesyncEngine engine(dcfg);

    if (!is_service) {
        std::cout << "===================================================================\n";
        std::cout << "        KILL F#CK DPI - Модульное ядро (C++17 Engine v1.5)         \n";
        std::cout << "===================================================================\n";
        std::cout << " [*] Стратегия: " << cfg.strategy << " (midsld SNI split + " << cfg.fooling << ")\n";
        std::cout << " [*] YouTube 4K: QUIC Drop + Fake TLS ClientHello (ВКЛ)\n";
        std::cout << " [*] Discord: Чат (TLS SNI) + Голосовые каналы (UDP 50000-65535 Fake STUN)\n";
        std::cout << " [*] Списки хостов: Google: " << kfdpi::HostlistManager::Instance().GetGoogleCount()
                  << " | General: " << kfdpi::HostlistManager::Instance().GetGeneralCount()
                  << " | Gaming: " << kfdpi::HostlistManager::Instance().GetGamingCount()
                  << " | Exclude: " << kfdpi::HostlistManager::Instance().GetExcludeCount() << "\n";
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
            g_stat_quic++;
            continue;
        }

        if (parsed.is_discord_voice) {
            g_stat_discord_voice++;
        }

        if (fragments.size() > 1 && parsed.l7 == kfdpi::L7Protocol::TLS_ClientHello) {
            g_stat_tls++;
        }

        for (const auto& frag : fragments) {
            if (frag.is_fake) {
                g_stat_fake++;
            }
            WinDivertSend(g_divert_handle, const_cast<uint8_t*>(frag.buffer.data()), static_cast<UINT>(frag.buffer.size()), nullptr, const_cast<WINDIVERT_ADDRESS*>(&frag.address));
        }

        if (!is_service) {
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_stats_print).count() >= 1000) {
                last_stats_print = now;
                std::cout << "\r[*] Статус: TLS: " << g_stat_tls
                          << " | Fake: " << g_stat_fake
                          << " | QUIC: " << g_stat_quic
                          << " | Discord Voice: " << g_stat_discord_voice << "   " << std::flush;
            }
        }
    }

    stop_divert();
}

void start_divert_worker() {
    stop_divert();
    g_worker_thread = std::thread([]() {
        packet_loop(true);
    });
}

int main(int argc, char* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    SetUnhandledExceptionFilter(CrashHandler);
    atexit(stop_divert);

    char exe_path[MAX_PATH];
    if (GetModuleFileNameA(NULL, exe_path, MAX_PATH)) {
        char* last_slash = strrchr(exe_path, '\\');
        if (last_slash) {
            *last_slash = '\0';
            SetCurrentDirectoryA(exe_path);
        }
    }

    // Предварительная очистка любых зависших драйверов от прошлых запусков
    kfdpi::ServiceController::ForceUnloadWinDivertKernelDriver();

    kfdpi::ConfigManager::Instance().Load("config.ini");
    kfdpi::HostlistManager::Instance().LoadLists(kfdpi::ConfigManager::Instance().Get().lists_dir);

    bool force_console = false;
    bool force_gui = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--service") {
            return kfdpi::ServiceController::RunAsService(SERVICE_NAME, []() {
                packet_loop(true);
            }, []() {
                stop_divert();
            });
        } else if (arg == "--console") {
            force_console = true;
        } else if (arg == "--gui") {
            force_gui = true;
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
            std::cout << "KILL F#CK DPI - Модульная C++ утилита обхода ТСПУ (версия 1.5)\n\n";
            std::cout << "Опции запуска:\n";
            std::cout << "  --gui             Запуск графической панели управления (по умолчанию при даблклике)\n";
            std::cout << "  --console         Запуск в интерактивном окне консоли с логом пакетов\n";
            std::cout << "  --install         Установить службу Windows (ручной запуск)\n";
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

    if (force_console) {
        SetConsoleCtrlHandler(console_handler, TRUE);
        packet_loop(false);
        return 0;
    }

    // Защита от одновременного запуска нескольких экземпляров GUI
    HANDLE hMutex = CreateMutexW(NULL, FALSE, L"Global\\KillFuckDpi_SingleInstance");
    if (hMutex != NULL && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing_hwnd = FindWindowW(L"KillFuckDpiGuiWindow", NULL);
        if (existing_hwnd) {
            ShowWindow(existing_hwnd, SW_RESTORE);
            SetForegroundWindow(existing_hwnd);
        }
        CloseHandle(hMutex);
        return 0;
    }

    // По умолчанию скрываем консольное окно и запускаем аккуратный Win32 Unicode GUI с треем
    HWND hConsole = GetConsoleWindow();
    if (hConsole) {
        ShowWindow(hConsole, SW_HIDE);
    }
    HINSTANCE hInst = GetModuleHandle(NULL);
    int res = kfdpi::GuiManager::Instance().Run(hInst, SW_NORMAL);
    if (hMutex) {
        CloseHandle(hMutex);
    }
    return res;
}
