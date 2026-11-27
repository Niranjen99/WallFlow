#pragma once

#include <windows.h>
#include <shellapi.h>  // NOTIFYICONDATAW (left out of windows.h by WIN32_LEAN_AND_MEAN)

namespace wallflow::win {

// The icon in the notification area (system tray). Clicks on it arrive at `owner` as
// `callback_message`, with the mouse message (e.g. WM_RBUTTONUP) in lParam.
class TrayIcon {
public:
    TrayIcon(HWND owner, UINT callback_message);
    ~TrayIcon();

    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;

    // Adds the icon. Called again after Explorer restarts, because the new taskbar does not
    // know about icons from before.
    bool show();

    // Shows the right-click menu at the mouse position and returns the chosen command id,
    // or 0 if the menu was dismissed.
    UINT show_menu();

    static constexpr UINT kQuit = 1;
    static constexpr UINT kEditSettings = 2;

private:
    NOTIFYICONDATAW data_{};
    HICON icon_ = nullptr;  // owned: destroyed with the tray icon
    bool shown_ = false;
};

}  // namespace wallflow::win
