#pragma once

#include <windows.h>

#include <filesystem>
#include <memory>

#include "wallflow/win/mpv_player.hpp"
#include "wallflow/win/tray_icon.hpp"
#include "wallflow/win/wallpaper_window.hpp"
#include "wallflow/win/window_events.hpp"
#include "wallflow/win/window_proc.hpp"

namespace wallflow::win {

// The whole app: a hidden controller window that receives tray clicks and system
// notifications, the tray icon, and the wallpaper window.
//
// The controller is a normal (hidden) top-level window, not a message-only window, because
// Windows sends the "TaskbarCreated" broadcast only to top-level windows.
class App {
public:
    // `video`: file to play in a loop; empty shows a solid colour.
    App(HINSTANCE instance, std::filesystem::path video);
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
    void start_player();

    // Pause rule: re-checks whether a window covers the monitor, then applies all reasons.
    void check_coverage();
    void update_pause();

    HINSTANCE instance_;
    HWND hwnd_ = nullptr;
    UINT taskbar_created_ = 0;  // message number Windows assigns to "TaskbarCreated"
    std::filesystem::path video_;
    std::unique_ptr<TrayIcon> tray_;
    // Declared after wallpaper_ on purpose: members are destroyed in reverse order, so the
    // player (which draws into the wallpaper window) always goes first.
    std::unique_ptr<WallpaperWindow> wallpaper_;
    std::unique_ptr<MpvPlayer> player_;
    int attach_attempts_ = 0;

    std::unique_ptr<WindowEvents> events_;
    HPOWERNOTIFY display_notify_ = nullptr;
    // Reasons to pause. Playback runs only when all are false.
    bool covered_ = false;       // a maximized or full-screen window covers the monitor
    bool locked_ = false;        // the session is locked (lock screen)
    bool display_off_ = false;   // the monitor is off (power saving)
    bool paused_ = false;        // what was last sent to the player
};

}  // namespace wallflow::win
