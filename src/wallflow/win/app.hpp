#pragma once

#include <windows.h>

#include <memory>

#include "wallflow/command_line.hpp"
#include "wallflow/win/tray_icon.hpp"
#include "wallflow/win/video_engine.hpp"
#include "wallflow/win/window_events.hpp"
#include "wallflow/win/window_proc.hpp"

namespace wallflow::win {

// The whole app: a hidden controller window that receives tray clicks and system
// notifications, the tray icon, and the video engine (two wallpaper windows and players).
//
// The controller is a normal (hidden) top-level window, not a message-only window, because
// Windows sends the "TaskbarCreated" broadcast only to top-level windows.
class App {
public:
    // `start.video`: file to play in a loop (start.loop: which part); empty shows nothing.
    App(HINSTANCE instance, CommandLine start);
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // Runs the message loop until the user quits. Returns the exit code.
    int run();

private:
    template <typename T>
    friend LRESULT CALLBACK window_proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT on_message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    // (Re)creates the wallpaper windows and attaches them; retries on a timer if the desktop
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
    CommandLine start_;
    std::unique_ptr<TrayIcon> tray_;
    std::unique_ptr<VideoEngine> engine_;
    bool engine_playing_ = false;  // play() was called on the current engine
    int attach_attempts_ = 0;

    std::unique_ptr<WindowEvents> events_;
    HPOWERNOTIFY display_notify_ = nullptr;
    // Reasons to pause. Playback runs only when all are false.
    bool covered_ = false;       // a maximized or full-screen window covers the monitor
    bool locked_ = false;        // the session is locked (lock screen)
    bool display_off_ = false;   // the monitor is off (power saving)
    bool paused_ = false;        // what was last sent to the engine
};

}  // namespace wallflow::win
