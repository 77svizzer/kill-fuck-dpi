#pragma once

#include <windows.h>
#include <shellapi.h>
#include <string>

namespace kfdpi {

class GuiManager {
public:
    static GuiManager& Instance() {
        static GuiManager inst;
        return inst;
    }

    int Run(HINSTANCE hInstance, int nCmdShow);
    void SetBypassState(bool active);
    bool IsBypassActive() const { return bypass_active_; }

private:
    GuiManager() = default;
    ~GuiManager();

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    void CreateTrayIcon(HWND hWnd);
    void RemoveTrayIcon();
    void ShowContextMenu(HWND hWnd);
    void UpdateStatusText();
    void DrawOwnerButton(LPDRAWITEMSTRUCT pDIS, const wchar_t* text, bool is_primary, bool is_danger = false);

    HWND hwnd_main_{NULL};
    HWND hwnd_btn_toggle_{NULL};
    HWND hwnd_btn_service_{NULL};
    HWND hwnd_btn_config_{NULL};
    HWND hwnd_btn_verify_{NULL};
    HWND hwnd_combo_preset_{NULL};

    HFONT hfont_title_{NULL};
    HFONT hfont_sub_{NULL};
    HFONT hfont_bold_{NULL};
    HFONT hfont_body_{NULL};
    HFONT hfont_stat_{NULL};

    HBRUSH hbr_bg_{NULL};
    HBRUSH hbr_card_{NULL};
    HPEN hpen_border_{NULL};

    NOTIFYICONDATAW nid_{};
    bool bypass_active_{false};
};

} // namespace kfdpi
