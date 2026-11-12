#include "GuiManager.hpp"
#include "ServiceController.hpp"
#include <commctrl.h>
#include <shellapi.h>
#include <thread>
#include <atomic>

#pragma comment(lib, "comctl32.lib")

#define WM_TRAYICON     (WM_USER + 100)
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

    const wchar_t CLASS_NAME[] = L"KillFuckDpiGuiWindow";

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    RegisterClassExW(&wc);

    int width = 440;
    int height = 290;
    int screen_w = GetSystemMetrics(SM_CXSCREEN);
    int screen_h = GetSystemMetrics(SM_CYSCREEN);
    int x = (screen_w - width) / 2;
    int y = (screen_h - height) / 2;

    hwnd_main_ = CreateWindowExW(
        0,
        CLASS_NAME,
        L"KILL F#CK DPI - Панель управления",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        x, y, width, height,
        NULL, NULL, hInstance, this
    );

    if (!hwnd_main_) return 1;

    // Создаем чистый системный шрифт Segoe UI для четкого рендеринга русского текста без сглаживания артефактов
    HFONT hFont = CreateFontW(
        16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );
    if (!hFont) {
        hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    }

    // Заголовок
    HWND hTitle = CreateWindowExW(0, L"STATIC", L"KILL F#CK DPI (Native C++ Engine)",
        WS_VISIBLE | WS_CHILD | SS_CENTER,
        20, 15, 385, 25, hwnd_main_, NULL, hInstance, NULL);
    SendMessageW(hTitle, WM_SETFONT, (WPARAM)hFont, TRUE);

    // Метка статуса
    hwnd_status_label_ = CreateWindowExW(0, L"STATIC", L"Статус: Обход отключен",
        WS_VISIBLE | WS_CHILD | SS_CENTER,
        20, 45, 385, 25, hwnd_main_, NULL, hInstance, NULL);
    SendMessageW(hwnd_status_label_, WM_SETFONT, (WPARAM)hFont, TRUE);

    // Главная кнопка запуска/остановки
    hwnd_btn_toggle_ = CreateWindowExW(0, L"BUTTON", L"Включить обход DPI",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        40, 80, 345, 40, hwnd_main_, (HMENU)ID_BTN_TOGGLE, hInstance, NULL);
    SendMessageW(hwnd_btn_toggle_, WM_SETFONT, (WPARAM)hFont, TRUE);

    // Кнопка экстренного сброса сети и выгрузки WinDivert
    hwnd_btn_service_ = CreateWindowExW(0, L"BUTTON", L"Экстренный сброс драйвера сети",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        40, 130, 345, 32, hwnd_main_, (HMENU)ID_BTN_SERVICE, hInstance, NULL);
    SendMessageW(hwnd_btn_service_, WM_SETFONT, (WPARAM)hFont, TRUE);

    // Кнопка открытия config.ini
    hwnd_btn_config_ = CreateWindowExW(0, L"BUTTON", L"Открыть настройки (config.ini)",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        40, 172, 345, 32, hwnd_main_, (HMENU)ID_BTN_CONFIG, hInstance, NULL);
    SendMessageW(hwnd_btn_config_, WM_SETFONT, (WPARAM)hFont, TRUE);

    CreateTrayIcon(hwnd_main_);
    ShowWindow(hwnd_main_, nCmdShow);
    UpdateWindow(hwnd_main_);

    MSG msg = {};
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    RemoveTrayIcon();
    return 0;
}

void GuiManager::CreateTrayIcon(HWND hWnd) {
    memset(&nid_, 0, sizeof(nid_));
    nid_.cbSize = sizeof(NOTIFYICONDATAW);
    nid_.hWnd = hWnd;
    nid_.uID = 1;
    nid_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid_.uCallbackMessage = WM_TRAYICON;
    nid_.hIcon = LoadIcon(NULL, IDI_SHIELD);
    lstrcpyW(nid_.szTip, L"KILL F#CK DPI");
    Shell_NotifyIconW(NIM_ADD, &nid_);
}

void GuiManager::RemoveTrayIcon() {
    if (nid_.cbSize) {
        Shell_NotifyIconW(NIM_DELETE, &nid_);
        memset(&nid_, 0, sizeof(nid_));
    }
}

void GuiManager::UpdateStatusText() {
    if (bypass_active_) {
        SetWindowTextW(hwnd_status_label_, L"Статус: Обход АКТИВЕН (YouTube 4K, Discord, Сайты)");
        SetWindowTextW(hwnd_btn_toggle_, L"Остановить обход DPI");
        lstrcpyW(nid_.szTip, L"KILL F#CK DPI - Активен");
    } else {
        SetWindowTextW(hwnd_status_label_, L"Статус: Обход отключен");
        SetWindowTextW(hwnd_btn_toggle_, L"Включить обход DPI");
        lstrcpyW(nid_.szTip, L"KILL F#CK DPI - Остановлен");
    }
    Shell_NotifyIconW(NIM_MODIFY, &nid_);
}

void GuiManager::SetBypassState(bool active) {
    bypass_active_ = active;
    UpdateStatusText();
}

void GuiManager::ShowContextMenu(HWND hWnd) {
    POINT pt;
    GetCursorPos(&pt);
    HMENU hMenu = CreatePopupMenu();
    InsertMenuW(hMenu, 0, MF_BYPOSITION | MF_STRING, ID_TRAY_RESTORE, L"Открыть окно");
    InsertMenuW(hMenu, 1, MF_BYPOSITION | MF_STRING, ID_TRAY_TOGGLE, bypass_active_ ? L"Остановить обход" : L"Запустить обход");
    InsertMenuW(hMenu, 2, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
    InsertMenuW(hMenu, 3, MF_BYPOSITION | MF_STRING, ID_TRAY_EXIT, L"Выход");

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
                stop_divert();
                ServiceController::ForceUnloadWinDivertKernelDriver();
                gui.SetBypassState(false);
                MessageBoxW(hWnd, L"Драйвер WinDivert успешно принудительно остановлен и выгружен из ядра Windows. Интернет работает напрямую.", L"Сброс сети выполнен", MB_OK | MB_ICONINFORMATION);
            } else if (wmId == ID_BTN_CONFIG) {
                ShellExecuteW(NULL, L"open", L"notepad.exe", L"config.ini", NULL, SW_NORMAL);
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
            return DefWindowProcW(hWnd, message, wParam, lParam);
    }
    return 0;
}

} // namespace kfdpi
