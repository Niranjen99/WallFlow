#include "wallflow/coverage.hpp"

#include <array>
#include <string_view>

namespace wallflow {

namespace {

// Windows that are part of the desktop itself, or ours.
constexpr std::array<std::wstring_view, 6> kIgnoredClasses{
    L"Progman",                 // the desktop
    L"WorkerW",                 // desktop wallpaper layer
    L"Shell_TrayWnd",           // taskbar
    L"Shell_SecondaryTrayWnd",  // taskbar on other monitors
    L"WallFlow.Controller",
    L"WallFlow.Wallpaper",
};

bool is_ignored(const std::wstring& class_name) {
    for (const auto ignored : kIgnoredClasses) {
        if (class_name == ignored) {
            return true;
        }
    }
    return false;
}

bool contains(const Rect& outer, const Rect& inner) {
    return outer.left <= inner.left && outer.top <= inner.top && outer.right >= inner.right &&
           outer.bottom >= inner.bottom;
}

bool center_inside(const Rect& window, const Rect& monitor) {
    const long cx = window.left + (window.right - window.left) / 2;
    const long cy = window.top + (window.bottom - window.top) / 2;
    return cx >= monitor.left && cx < monitor.right && cy >= monitor.top && cy < monitor.bottom;
}

}  // namespace

bool is_monitor_covered(const Rect& monitor, std::span<const WindowState> windows) {
    for (const WindowState& w : windows) {
        if (!w.visible || w.minimized || w.cloaked || w.click_through || is_ignored(w.class_name)) {
            continue;
        }
        // A maximized window is slightly larger than the work area (its invisible resize
        // borders hang over the edges by a few pixels) and leaves the taskbar uncovered, so it
        // is matched by "maximized, and its centre is on this monitor".
        if (w.maximized && center_inside(w.rect, monitor)) {
            return true;
        }
        // Full screen: the window covers every pixel of the monitor.
        if (contains(w.rect, monitor)) {
            return true;
        }
    }
    return false;
}

}  // namespace wallflow
