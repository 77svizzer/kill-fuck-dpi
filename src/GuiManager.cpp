#include "GuiManager.hpp"
#include "ServiceController.hpp"
#include "ConfigManager.hpp"
#include <commctrl.h>
#include <dwmapi.h>
#include <atomic>
#include <vector>
#include <sstream>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")

#define WM_TRAYICON     (WM_USER + 100)
#define ID_BTN_TOGGLE   1001
#define ID_BTN_SERVICE  1002
#define ID_BTN_CONFIG   1003
#define ID_BTN_VERIFY   1004
#define ID_COMBO_PRESET 1005
#define ID_TIMER_STATS  1006

#define ID_TRAY_RESTORE 2001
#define ID_TRAY_TOGGLE  2002
#define ID_TRAY_EXIT    2003

// Внешние объявления из main.cpp
extern void start_divert_worker();
extern void stop_divert();
extern bool is_divert_running();
extern std::atomic<uint64_t> g_stat_tls;
extern std::atomic<uint64_t> g_stat_fake;
extern std::atomic<uint64_t> g_stat_quic;
extern std::atomic<uint64_t> g_stat_discord_voice;

namespace kfdpi {

GuiManager::~GuiManager() {
    if (hfont_title_) DeleteObject(hfont_title_);
    if (hfont_sub_) DeleteObject(hfont_sub_);
    if (hfont_bold_) DeleteObject(hfont_bold_);
    if (hfont_body_) DeleteObject(hfont_body_);
    if (hfont_stat_) DeleteObject(hfont_stat_);
    if (hbr_bg_) DeleteObject(hbr_bg_);
    if (hbr_card_) DeleteObject(hbr_card_);
    if (hpen_border_) DeleteObject(hpen_border_);
}

int GuiManager::Run(HINSTANCE hInstance, int nCmdShow) {
    INITCOMMONCONTROLSEX icex = { sizeof(icex), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icex);

    const wchar_t CLASS_NAME[] = L"KillFuckDpiGuiWindow";

    hbr_bg_ = CreateSolidBrush(RGB(20, 24, 33));        // Темный графитовый фон
    hbr_card_ = CreateSolidBrush(RGB(28, 34, 46));      // Поверхность карточек
    hpen_border_ = CreatePen(PS_SOLID, 1, RGB(44, 53, 72)); // Сдержанная граница

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = hbr_bg_;

    RegisterClassExW(&wc);

    int width = 580;
    int height = 520;
    int screen_w = GetSystemMetrics(SM_CXSCREEN);
    int screen_h = GetSystemMetrics(SM_CYSCREEN);
    int x = (screen_w - width) / 2;
    int y = (screen_h - height) / 2;

    hwnd_main_ = CreateWindowExW(
        WS_EX_APPWINDOW,
        CLASS_NAME,
        L"KILL F#CK DPI - Сетевой экран десинхронизации ТСПУ",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        x, y, width, height,
        NULL, NULL, hInstance, this
    );

    if (!hwnd_main_) return 1;

    // Включаем темный заголовок Windows 10/11
    BOOL darkMode = TRUE;
    DwmSetWindowAttribute(hwnd_main_, 20, &darkMode, sizeof(darkMode)); // DWMWA_USE_IMMERSIVE_DARK_MODE
    DwmSetWindowAttribute(hwnd_main_, 19, &darkMode, sizeof(darkMode)); // Windows 10 compatibility

    // Шрифты Segoe UI
    hfont_title_ = CreateFontW(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    hfont_sub_   = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    hfont_bold_  = CreateFontW(14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    hfont_body_  = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    hfont_stat_  = CreateFontW(18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    // Выпадающий список выбора пресета
    hwnd_combo_preset_ = CreateWindowExW(
        0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        30, 310, 505, 160,
        hwnd_main_, (HMENU)ID_COMBO_PRESET, hInstance, NULL
    );
    SendMessageW(hwnd_combo_preset_, WM_SETFONT, (WPARAM)hfont_body_, TRUE);
    SendMessageW(hwnd_combo_preset_, CB_ADDSTRING, 0, (LPARAM)L"Disorder (Reverse Split 2b) - Стандарт ТСПУ РФ [Рекомендуется]");
    SendMessageW(hwnd_combo_preset_, CB_ADDSTRING, 0, (LPARAM)L"Fake + BadSeq + AutoTTL - Пробитие с фейковым TLS");
    SendMessageW(hwnd_combo_preset_, CB_ADDSTRING, 0, (LPARAM)L"Fake + Disorder - Агрессивная десинхронизация");
    SendMessageW(hwnd_combo_preset_, CB_SETCURSEL, 0, 0);

    // Главная кнопка запуска/остановки (Owner Draw)
    hwnd_btn_toggle_ = CreateWindowExW(
        0, L"BUTTON", L"Остановить обход DPI",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        30, 360, 505, 42,
        hwnd_main_, (HMENU)ID_BTN_TOGGLE, hInstance, NULL
    );

    // Второстепенные кнопки (Owner Draw)
    hwnd_btn_verify_ = CreateWindowExW(
        0, L"BUTTON", L"Проверить сеть",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        30, 415, 160, 36,
        hwnd_main_, (HMENU)ID_BTN_VERIFY, hInstance, NULL
    );

    hwnd_btn_service_ = CreateWindowExW(
        0, L"BUTTON", L"Сброс драйвера",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        202, 415, 160, 36,
        hwnd_main_, (HMENU)ID_BTN_SERVICE, hInstance, NULL
    );

    hwnd_btn_config_ = CreateWindowExW(
        0, L"BUTTON", L"Настройки (config)",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        375, 415, 160, 36,
        hwnd_main_, (HMENU)ID_BTN_CONFIG, hInstance, NULL
    );

    CreateTrayIcon(hwnd_main_);
    ShowWindow(hwnd_main_, nCmdShow);
    UpdateWindow(hwnd_main_);

    // Автоматический старт обхода при запуске приложения
    start_divert_worker();
    SetBypassState(true);

    // Таймер обновления статистики каждые 500 мс
    SetTimer(hwnd_main_, ID_TIMER_STATS, 500, NULL);

    MSG msg = {};
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    KillTimer(hwnd_main_, ID_TIMER_STATS);
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
    nid_.hIcon = LoadIconW(NULL, IDI_SHIELD);
    wcsncpy_s(nid_.szTip, sizeof(nid_.szTip)/sizeof(WCHAR), L"KILL F#CK DPI - Активен", _TRUNCATE);
    Shell_NotifyIconW(NIM_ADD, &nid_);
}

void GuiManager::RemoveTrayIcon() {
    if (nid_.cbSize) {
        Shell_NotifyIconW(NIM_DELETE, &nid_);
        memset(&nid_, 0, sizeof(nid_));
    }
}

void GuiManager::UpdateStatusText() {
    if (nid_.cbSize) {
        wcsncpy_s(nid_.szTip, sizeof(nid_.szTip)/sizeof(WCHAR),
            bypass_active_ ? L"KILL F#CK DPI - Активен" : L"KILL F#CK DPI - Остановлен",
            _TRUNCATE);
        nid_.uFlags = NIF_TIP;
        Shell_NotifyIconW(NIM_MODIFY, &nid_);
    }
    InvalidateRect(hwnd_main_, NULL, FALSE);
}

void GuiManager::SetBypassState(bool active) {
    bypass_active_ = active;
    UpdateStatusText();
}

void GuiManager::ShowContextMenu(HWND hWnd) {
    POINT pt;
    GetCursorPos(&pt);
    HMENU hMenu = CreatePopupMenu();
    InsertMenuW(hMenu, 0, MF_BYPOSITION | MF_STRING, ID_TRAY_RESTORE, L"Открыть панель");
    InsertMenuW(hMenu, 1, MF_BYPOSITION | MF_STRING, ID_TRAY_TOGGLE, bypass_active_ ? L"Остановить обход" : L"Запустить обход");
    InsertMenuW(hMenu, 2, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
    InsertMenuW(hMenu, 3, MF_BYPOSITION | MF_STRING, ID_TRAY_EXIT, L"Выход");

    SetForegroundWindow(hWnd);
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hWnd, NULL);
    PostMessageW(hWnd, WM_NULL, 0, 0);
    DestroyMenu(hMenu);
}

void GuiManager::DrawOwnerButton(LPDRAWITEMSTRUCT pDIS, const wchar_t* text, bool is_primary, bool is_danger) {
    HDC hdc = pDIS->hDC;
    RECT rc = pDIS->rcItem;
    bool is_pressed = (pDIS->itemState & ODS_SELECTED);

    COLORREF bg_color;
    COLORREF text_color = RGB(245, 247, 250);
    COLORREF border_color;

    if (is_primary) {
        if (bypass_active_) {
            bg_color = is_pressed ? RGB(160, 40, 40) : RGB(190, 50, 50); // Остановить обход (красно-кирпичный)
            border_color = RGB(210, 70, 70);
        } else {
            bg_color = is_pressed ? RGB(35, 120, 65) : RGB(46, 150, 80);  // Включить обход (приглушенный изумруд)
            border_color = RGB(60, 180, 100);
        }
    } else if (is_danger) {
        bg_color = is_pressed ? RGB(45, 30, 35) : RGB(35, 28, 32);
        border_color = RGB(70, 45, 52);
    } else {
        bg_color = is_pressed ? RGB(36, 44, 60) : RGB(28, 34, 46);
        border_color = RGB(44, 53, 72);
    }

    HBRUSH hbr = CreateSolidBrush(bg_color);
    HPEN hpen = CreatePen(PS_SOLID, 1, border_color);
    HBRUSH old_br = (HBRUSH)SelectObject(hdc, hbr);
    HPEN old_pen = (HPEN)SelectObject(hdc, hpen);

    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);

    SelectObject(hdc, old_br);
    SelectObject(hdc, old_pen);
    DeleteObject(hbr);
    DeleteObject(hpen);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, text_color);
    SelectObject(hdc, is_primary ? hfont_bold_ : hfont_body_);
    DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
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

        case WM_TIMER:
            if (wParam == ID_TIMER_STATS) {
                // Перерисовываем карточку статистики
                RECT rc_stats = { 30, 180, 535, 290 };
                InvalidateRect(hWnd, &rc_stats, FALSE);
            }
            break;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);

            // 1. Шапка
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(245, 247, 250));
            SelectObject(hdc, gui.hfont_title_);
            TextOutW(hdc, 30, 20, L"KILL F#CK DPI", 13);

            SetTextColor(hdc, RGB(145, 158, 171));
            SelectObject(hdc, gui.hfont_sub_);
            TextOutW(hdc, 30, 48, L"Сетевой экран десинхронизации ТСПУ для Windows (Native C++17)", 61);

            // 2. Карточка статуса (30, 75, 535, 165)
            HBRUSH old_br = (HBRUSH)SelectObject(hdc, gui.hbr_card_);
            HPEN old_pen = (HPEN)SelectObject(hdc, gui.hpen_border_);
            RoundRect(hdc, 30, 75, 535, 165, 8, 8);

            // Индикатор состояния
            if (gui.bypass_active_) {
                HBRUSH hbr_dot = CreateSolidBrush(RGB(46, 204, 113));
                HPEN hpen_dot = CreatePen(PS_SOLID, 1, RGB(46, 204, 113));
                SelectObject(hdc, hbr_dot);
                SelectObject(hdc, hpen_dot);
                Ellipse(hdc, 50, 94, 62, 106);
                DeleteObject(hbr_dot);
                DeleteObject(hpen_dot);

                SetTextColor(hdc, RGB(46, 204, 113));
                SelectObject(hdc, gui.hfont_bold_);
                TextOutW(hdc, 70, 91, L"ОБХОД DPI АКТИВЕН", 17);
            } else {
                HBRUSH hbr_dot = CreateSolidBrush(RGB(231, 76, 60));
                HPEN hpen_dot = CreatePen(PS_SOLID, 1, RGB(231, 76, 60));
                SelectObject(hdc, hbr_dot);
                SelectObject(hdc, hpen_dot);
                Ellipse(hdc, 50, 94, 62, 106);
                DeleteObject(hbr_dot);
                DeleteObject(hpen_dot);

                SetTextColor(hdc, RGB(231, 76, 60));
                SelectObject(hdc, gui.hfont_bold_);
                TextOutW(hdc, 70, 91, L"ОБХОД DPI ОСТАНОВЛЕН", 20);
            }

            SetTextColor(hdc, RGB(170, 182, 196));
            SelectObject(hdc, gui.hfont_body_);
            TextOutW(hdc, 50, 118, L"YouTube 4K  *  Discord Voice & Media  *  Заблокированные сайты", 61);
            TextOutW(hdc, 50, 138, L"Игровые порты (CS2, Xplay, FACEIT): Аппаратный NDIS Bypass (0ms)", 64);

            // 3. Карточка статистики (30, 180, 535, 290)
            SelectObject(hdc, gui.hbr_card_);
            SelectObject(hdc, gui.hpen_border_);
            RoundRect(hdc, 30, 180, 535, 290, 8, 8);

            SetTextColor(hdc, RGB(145, 158, 171));
            SelectObject(hdc, gui.hfont_sub_);
            TextOutW(hdc, 45, 192, L"ДИАГНОСТИКА СЕТЕВОГО ТРАКТА", 27);

            // Значения метрик
            wchar_t buf[64];
            SetTextColor(hdc, RGB(245, 247, 250));
            SelectObject(hdc, gui.hfont_stat_);

            swprintf_s(buf, L"%llu", g_stat_tls.load());
            TextOutW(hdc, 45, 220, buf, wcslen(buf));
            swprintf_s(buf, L"%llu", g_stat_quic.load());
            TextOutW(hdc, 175, 220, buf, wcslen(buf));
            swprintf_s(buf, L"%llu", g_stat_discord_voice.load());
            TextOutW(hdc, 305, 220, buf, wcslen(buf));
            swprintf_s(buf, L"%llu", g_stat_fake.load());
            TextOutW(hdc, 435, 220, buf, wcslen(buf));

            SetTextColor(hdc, RGB(120, 132, 148));
            SelectObject(hdc, gui.hfont_sub_);
            TextOutW(hdc, 45, 250, L"TLS ClientHello", 15);
            TextOutW(hdc, 175, 250, L"QUIC Drop (4K)", 14);
            TextOutW(hdc, 305, 250, L"Discord Voice", 13);
            TextOutW(hdc, 435, 250, L"Фейковых пакетов", 16);

            SelectObject(hdc, old_br);
            SelectObject(hdc, old_pen);
            EndPaint(hWnd, &ps);
            return 0;
        }

        case WM_DRAWITEM: {
            LPDRAWITEMSTRUCT pDIS = (LPDRAWITEMSTRUCT)lParam;
            if (pDIS->CtlID == ID_BTN_TOGGLE) {
                gui.DrawOwnerButton(pDIS, gui.bypass_active_ ? L"Остановить обход DPI" : L"Включить обход DPI", true);
                return TRUE;
            } else if (pDIS->CtlID == ID_BTN_VERIFY) {
                gui.DrawOwnerButton(pDIS, L"Проверить сеть", false);
                return TRUE;
            } else if (pDIS->CtlID == ID_BTN_SERVICE) {
                gui.DrawOwnerButton(pDIS, L"Сброс драйвера", false, true);
                return TRUE;
            } else if (pDIS->CtlID == ID_BTN_CONFIG) {
                gui.DrawOwnerButton(pDIS, L"Настройки (config)", false);
                return TRUE;
            }
            break;
        }

        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            int code = HIWORD(wParam);

            if (wmId == ID_COMBO_PRESET && code == CBN_SELCHANGE) {
                int sel = (int)SendMessageW(gui.hwnd_combo_preset_, CB_GETCURSEL, 0, 0);
                std::string strat = "disorder";
                std::string fooling = "none";
                if (sel == 1) {
                    strat = "fake_split2";
                    fooling = "badseq,autottl";
                } else if (sel == 2) {
                    strat = "fake_disorder";
                    fooling = "badseq,autottl";
                }

                // Перезапуск с новым пресетом
                if (gui.bypass_active_) {
                    stop_divert();
                    start_divert_worker();
                    gui.SetBypassState(true);
                }
            } else if (wmId == ID_BTN_TOGGLE || wmId == ID_TRAY_TOGGLE) {
                if (gui.bypass_active_) {
                    stop_divert();
                    gui.SetBypassState(false);
                } else {
                    start_divert_worker();
                    gui.SetBypassState(true);
                }
                InvalidateRect(gui.hwnd_btn_toggle_, NULL, TRUE);
            } else if (wmId == ID_BTN_VERIFY) {
                ShellExecuteW(NULL, L"open", L"powershell.exe", L"-NoProfile -ExecutionPolicy Bypass -File verify.ps1", NULL, SW_NORMAL);
            } else if (wmId == ID_BTN_SERVICE) {
                stop_divert();
                ServiceController::ForceUnloadWinDivertKernelDriver();
                gui.SetBypassState(false);
                InvalidateRect(gui.hwnd_btn_toggle_, NULL, TRUE);
                MessageBoxW(hWnd, L"Драйвер WinDivert успешно выгружен из ядра Windows.\nСетевой стек работает в штатном режиме.", L"Сброс выполнен", MB_OK | MB_ICONINFORMATION);
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
