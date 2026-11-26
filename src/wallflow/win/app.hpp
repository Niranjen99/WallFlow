#pragma once

#include <windows.h>

#include <memory>

#include "wallflow/win/tray_icon.hpp"
#include "wallflow/win/wallpaper_window.hpp"
#include "wallflow/win/window_proc.hpp"

namespace wallflow::win {

// The whole app: a hidden controller window that receives tray clicks and system
// notifications, the tray icon, and the wallpaper window.
//
// The controller is a normal (hidden) top-level window, not a message-only window, because
// Windows sends the "TaskbarCreated" broadcast only to top-level windows.
class App {
public:
    explicit App(HINSTANCE instance);
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // Runs the message loop until the user quits. Returns the exit code.
    int run();

private:
    template <typename T>
    friend LRESULT CALLBACK window_proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT on_message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    // (Re)creates the wallpaper window and attaches it; retries on a timer if the desktop
    // is not ready yet (e.g. right after Explorer restarts).
    void start_wallpaper();
    void try_attach();
    void quit();

    HINSTANCE instance_;
    HWND hwnd_ = nullptr;
    UINT taskbar_created_ = 0;  // message number Windows assigns to "TaskbarCreated"
    std::unique_ptr<TrayIcon> tray_;
    std::unique_ptr<WallpaperWindow> wallpaper_;
    int attach_attempts_ = 0;
};

}  // namespace wallflow::win
