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
    ~GuiManager() {
        if (hfont_) {
            DeleteObject(hfont_);
            hfont_ = NULL;
        }
    }

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    void CreateTrayIcon(HWND hWnd);
    void RemoveTrayIcon();
    void ShowContextMenu(HWND hWnd);
    void UpdateStatusText();

    HWND hwnd_main_{NULL};
    HWND hwnd_status_label_{NULL};
    HWND hwnd_btn_toggle_{NULL};
    HWND hwnd_btn_service_{NULL};
    HWND hwnd_btn_config_{NULL};
    HFONT hfont_{NULL};
    NOTIFYICONDATAW nid_{};
    bool bypass_active_{false};
    bool service_installed_{false};
};

} // namespace kfdpi
