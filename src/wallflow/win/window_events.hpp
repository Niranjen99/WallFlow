#pragma once

#include <windows.h>

#include <vector>

#include "wallflow/coverage.hpp"

namespace wallflow::win {

// Watches other programs' windows through WinEvent hooks and, whenever something happens that
// could change whether the screen is covered (focus moves, a window is shown, hidden,
// minimized, restored, cloaked, moved or resized), (re)starts a timer on `window`. A burst of
// events, such as dragging a window, therefore ends in a single WM_TIMER `delay_ms` after it
// calms down. No polling: when nothing happens, nothing runs.
//
// Only one instance may exist at a time (the hook callback has no context pointer, see .cpp).
// Create and destroy it on the thread that runs the message loop.
class WindowEvents {
public:
    WindowEvents(HWND window, UINT_PTR timer_id, UINT delay_ms);
    ~WindowEvents();

    WindowEvents(const WindowEvents&) = delete;
    WindowEvents& operator=(const WindowEvents&) = delete;

private:
    static void CALLBACK on_event(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG id_object,
                                  LONG id_child, DWORD thread, DWORD time);
    void watch_moves_of(HWND foreground);

    HWND window_;
    UINT_PTR timer_id_;
    UINT delay_ms_;
    std::vector<HWINEVENTHOOK> hooks_;
    HWINEVENTHOOK move_hook_ = nullptr;  // location changes, only for the foreground process
    DWORD move_process_ = 0;
};

// The facts is_monitor_covered() needs, for every visible top-level window.
std::vector<WindowState> snapshot_windows();

}  // namespace wallflow::win
