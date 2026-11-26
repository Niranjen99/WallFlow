#pragma once

#include <span>
#include <string>

namespace wallflow {

// Same layout and meaning as the Windows RECT: right and bottom are one past the last pixel.
struct Rect {
    long left = 0;
    long top = 0;
    long right = 0;
    long bottom = 0;
};

// What we know about one top-level window, collected by the Win32 code (win/window_events).
struct WindowState {
    Rect rect;                // screen coordinates (GetWindowRect)
    std::wstring class_name;
    bool visible = true;      // WS_VISIBLE
    bool minimized = false;
    bool maximized = false;
    bool cloaked = false;     // exists and "visible", but DWM does not draw it (suspended apps,
                              // other virtual desktops, hidden system windows)
    bool click_through = false;  // WS_EX_TRANSPARENT: overlays that let clicks pass through
};

// The pause rule: the wallpaper on `monitor` should pause when a real window covers it, i.e. a
// maximized window on that monitor or a window as big as the whole monitor (full-screen games
// and videos). Desktop, taskbar and WallFlow's own windows never count, and neither do
// windows that cannot actually be seen (hidden, minimized, cloaked, click-through overlays).
bool is_monitor_covered(const Rect& monitor, std::span<const WindowState> windows);

}  // namespace wallflow
