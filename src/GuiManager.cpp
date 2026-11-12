#include "GuiManager.hpp"
#include "ServiceController.hpp"
#include <commctrl.h>
#include <shellapi.h>
#include <thread>
#include <atomic>

#pragma comment(lib, "comctl32.lib")

#define WM_TRAYICON (WM_USER + 100)
#define ID_BTN_TOGGLE   1001
#define ID_BTN_SERVICE  1002
#define ID_BTN_CONFIG   1003
#define ID_TRAY_RESTORE 1004
#define ID_TRAY_TOGGLE  1005
#define ID_TRAY_EXIT    1006

// Внешние объявления из main.cpp для запуска/остановки перехвата
extern void start_divert_worker();
extern void stop_divert();
extern bool is_divert_running();

namespace kfdpi {

int GuiManager::Run(HINSTANCE hInstance, int nCmdShow) {
    INITCOMMONCONTROLSEX icex = { sizeof(icex), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icex);

    const char CLASS_NAME[] = "KillFuckDpiGuiWindow";

    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    RegisterClassExA(&wc);

    int width = 420;
    int height = 280;
    int screen_w = GetSystemMetrics(SM_CXSCREEN);
    int screen_h = GetSystemMetrics(SM_CYSCREEN);
    int x = (screen_w - width) / 2;
    int y = (screen_h - height) / 2;

    hwnd_main_ = CreateWindowExA(
        0,
        CLASS_NAME,
        "KILL F#CK DPI - Панель управления",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        x, y, width, height,
        NULL, NULL, hInstance, this
    );

    if (!hwnd_main_) return 1;

    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    // Заголовок
    HWND hTitle = CreateWindowExA(0, "STATIC", "KILL F#CK DPI (Native C++ Engine)",
        WS_VISIBLE | WS_CHILD | SS_CENTER,
        20, 15, 360, 25, hwnd_main_, NULL, hInstance, NULL);
    SendMessageA(hTitle, WM_SETFONT, (WPARAM)hFont, TRUE);

    // Метка статуса
    hwnd_status_label_ = CreateWindowExA(0, "STATIC", "Статус: Обход отключен",
        WS_VISIBLE | WS_CHILD | SS_CENTER,
        20, 45, 360, 25, hwnd_main_, NULL, hInstance, NULL);
    SendMessageA(hwnd_status_label_, WM_SETFONT, (WPARAM)hFont, TRUE);

    // Главная кнопка запуска/остановки
    hwnd_btn_toggle_ = CreateWindowExA(0, "BUTTON", "Включить обход DPI",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        40, 80, 320, 40, hwnd_main_, (HMENU)ID_BTN_TOGGLE, hInstance, NULL);
    SendMessageA(hwnd_btn_toggle_, WM_SETFONT, (WPARAM)hFont, TRUE);

    // Кнопка службы Windows
    hwnd_btn_service_ = CreateWindowExA(0, "BUTTON", "Установить службу автозапуска",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        40, 130, 320, 32, hwnd_main_, (HMENU)ID_BTN_SERVICE, hInstance, NULL);
    SendMessageA(hwnd_btn_service_, WM_SETFONT, (WPARAM)hFont, TRUE);

    // Кнопка открытия config.ini
    hwnd_btn_config_ = CreateWindowExA(0, "BUTTON", "Открыть настройки (config.ini)",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        40, 172, 320, 32, hwnd_main_, (HMENU)ID_BTN_CONFIG, hInstance, NULL);
    SendMessageA(hwnd_btn_config_, WM_SETFONT, (WPARAM)hFont, TRUE);

    CreateTrayIcon(hwnd_main_);
    ShowWindow(hwnd_main_, nCmdShow);
    UpdateWindow(hwnd_main_);

    MSG msg = {};
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    RemoveTrayIcon();
    return 0;
}

void GuiManager::CreateTrayIcon(HWND hWnd) {
    memset(&nid_, 0, sizeof(nid_));
    nid_.cbSize = sizeof(NOTIFYICONDATAA);
    nid_.hWnd = hWnd;
    nid_.uID = 1;
    nid_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid_.uCallbackMessage = WM_TRAYICON;
    nid_.hIcon = LoadIcon(NULL, IDI_SHIELD);
    lstrcpyA(nid_.szTip, "KILL F#CK DPI");
    Shell_NotifyIconA(NIM_ADD, &nid_);
}

void GuiManager::RemoveTrayIcon() {
    if (nid_.cbSize) {
        Shell_NotifyIconA(NIM_DELETE, &nid_);
        memset(&nid_, 0, sizeof(nid_));
    }
}

void GuiManager::UpdateStatusText() {
    if (bypass_active_) {
        SetWindowTextA(hwnd_status_label_, "Статус: Обход АКТИВЕН (YouTube 4K, Discord, Сайты)");
        SetWindowTextA(hwnd_btn_toggle_, "Остановить обход DPI");
        lstrcpyA(nid_.szTip, "KILL F#CK DPI - Активен");
    } else {
        SetWindowTextA(hwnd_status_label_, "Статус: Обход отключен");
        SetWindowTextA(hwnd_btn_toggle_, "Включить обход DPI");
        lstrcpyA(nid_.szTip, "KILL F#CK DPI - Остановлен");
    }
    Shell_NotifyIconA(NIM_MODIFY, &nid_);
}

void GuiManager::SetBypassState(bool active) {
    bypass_active_ = active;
    UpdateStatusText();
}

void GuiManager::ShowContextMenu(HWND hWnd) {
    POINT pt;
    GetCursorPos(&pt);
    HMENU hMenu = CreatePopupMenu();
    InsertMenuA(hMenu, 0, MF_BYPOSITION | MF_STRING, ID_TRAY_RESTORE, "Открыть окно");
    InsertMenuA(hMenu, 1, MF_BYPOSITION | MF_STRING, ID_TRAY_TOGGLE, bypass_active_ ? "Остановить обход" : "Запустить обход");
    InsertMenuA(hMenu, 2, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
    InsertMenuA(hMenu, 3, MF_BYPOSITION | MF_STRING, ID_TRAY_EXIT, "Выход");

    SetForegroundWindow(hWnd);
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hWnd, NULL);
    DestroyMenu(hMenu);
}

LRESULT CALLBACK GuiManager::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    GuiManager& gui = GuiManager::Instance();

    switch (message) {
        case WM_SYSCOMMAND:
            if ((wParam & 0xFFF0) == SC_MINIMIZE) {
                ShowWindow(hWnd, SW_HIDE);
                return 0;
            }
            break;

        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            if (wmId == ID_BTN_TOGGLE || wmId == ID_TRAY_TOGGLE) {
                if (gui.bypass_active_) {
                    stop_divert();
                    gui.SetBypassState(false);
                } else {
                    start_divert_worker();
                    gui.SetBypassState(true);
                }
            } else if (wmId == ID_BTN_SERVICE) {
                char current_exe[MAX_PATH];
                GetModuleFileNameA(NULL, current_exe, MAX_PATH);
                if (!gui.service_installed_) {
                    ServiceController::Install("KillFuckDPI", "KILL F#CK DPI Service", current_exe);
                    ServiceController::Start("KillFuckDPI");
                    gui.service_installed_ = true;
                    SetWindowTextA(gui.hwnd_btn_service_, "Удалить службу автозапуска");
                    MessageBoxA(hWnd, "Служба успешно установлена и работает в фоне при включении ПК!", "Успех", MB_OK | MB_ICONINFORMATION);
                } else {
                    ServiceController::Stop("KillFuckDPI");
                    ServiceController::Uninstall("KillFuckDPI");
                    gui.service_installed_ = false;
                    SetWindowTextA(gui.hwnd_btn_service_, "Установить службу автозапуска");
                    MessageBoxA(hWnd, "Служба успешно удалена.", "Успех", MB_OK | MB_ICONINFORMATION);
                }
            } else if (wmId == ID_BTN_CONFIG) {
                ShellExecuteA(NULL, "open", "notepad.exe", "config.ini", NULL, SW_NORMAL);
            } else if (wmId == ID_TRAY_RESTORE) {
                ShowWindow(hWnd, SW_SHOW);
                SetForegroundWindow(hWnd);
            } else if (wmId == ID_TRAY_EXIT) {
                stop_divert();
                DestroyWindow(hWnd);
            }
            break;
        }

        case WM_TRAYICON:
            if (lParam == WM_LBUTTONUP) {
                ShowWindow(hWnd, SW_SHOW);
                SetForegroundWindow(hWnd);
            } else if (lParam == WM_RBUTTONUP) {
                gui.ShowContextMenu(hWnd);
            }
            break;

        case WM_DESTROY:
            stop_divert();
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProcA(hWnd, message, wParam, lParam);
    }
    return 0;
}

} // namespace kfdpi
