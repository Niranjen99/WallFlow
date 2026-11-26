#include "wallflow/win/desktop.hpp"

#include <array>
#include <cstdint>

namespace wallflow::win {

namespace {

// Undocumented message that makes Explorer create the WorkerW wallpaper layer behind the
// icons. There is no official API for this; every live wallpaper app relies on it.
constexpr UINT kSpawnWallpaperLayer = 0x052C;

std::uintptr_t to_id(HWND hwnd) { return reinterpret_cast<std::uintptr_t>(hwnd); }
HWND to_hwnd(std::uintptr_t id) { return reinterpret_cast<HWND>(id); }

std::wstring class_of(HWND hwnd) {
    std::array<wchar_t, 256> name{};
    const int length = ::GetClassNameW(hwnd, name.data(), static_cast<int>(name.size()));
    return std::wstring(name.data(), length > 0 ? static_cast<std::size_t>(length) : 0);
}

// EnumWindows and EnumChildWindows call back a plain function once per window. The extra
// LPARAM argument carries a pointer to our vector through the Windows API and back.
BOOL CALLBACK collect_window(HWND hwnd, LPARAM lparam) {
    auto* list = reinterpret_cast<std::vector<HWND>*>(lparam);
    list->push_back(hwnd);
    return TRUE;  // keep going
}

std::vector<HWND> top_level_windows() {
    std::vector<HWND> list;
    ::EnumWindows(collect_window, reinterpret_cast<LPARAM>(&list));
    return list;
}

std::vector<HWND> direct_children(HWND parent) {
    std::vector<HWND> all;  // EnumChildWindows also reports grandchildren
    ::EnumChildWindows(parent, collect_window, reinterpret_cast<LPARAM>(&all));
    std::vector<HWND> direct;
    for (HWND child : all) {
        if (::GetAncestor(child, GA_PARENT) == parent) {
            direct.push_back(child);
        }
    }
    return direct;
}

}  // namespace

std::vector<WindowInfo> snapshot_desktop() {
    std::vector<WindowInfo> windows;
    for (HWND top : top_level_windows()) {
        std::wstring name = class_of(top);
        const bool look_inside = (name == L"Progman" || name == L"WorkerW");
        windows.push_back({to_id(top), 0, std::move(name)});
        if (look_inside) {
            for (HWND child : direct_children(top)) {
                windows.push_back({to_id(child), to_id(top), class_of(child)});
            }
        }
    }
    return windows;
}

std::optional<DesktopLayout> attach_behind_icons(HWND window) {
    HWND progman = ::FindWindowW(L"Progman", nullptr);
    if (progman == nullptr) {
        return std::nullopt;  // Explorer is not running (yet)
    }
    // Harmless if the layer already exists. The timeout stops a hung Explorer from hanging us.
    DWORD_PTR ignored = 0;
    ::SendMessageTimeoutW(progman, kSpawnWallpaperLayer, 0, 0, SMTO_NORMAL, 1000, &ignored);

    const auto snapshot = snapshot_desktop();
    const auto plan = plan_attach(snapshot);
    if (!plan) {
        return std::nullopt;
    }
    HWND parent = to_hwnd(plan->parent);

    // A window becoming a child must have WS_CHILD instead of WS_POPUP *before* SetParent,
    // or Windows keeps treating it as a top-level window (documented in SetParent's remarks).
    LONG_PTR style = ::GetWindowLongPtrW(window, GWL_STYLE);
    style = (style & ~static_cast<LONG_PTR>(WS_POPUP)) | WS_CHILD;
    ::SetWindowLongPtrW(window, GWL_STYLE, style);
    // SetParent returns the old parent, which is NULL for a top-level window, so NULL alone
    // does not mean failure. Clear the error code first and check it afterwards.
    ::SetLastError(ERROR_SUCCESS);
    if (::SetParent(window, parent) == nullptr && ::GetLastError() != ERROR_SUCCESS) {
        return std::nullopt;
    }

    // Cover the primary monitor. A child window is positioned in its parent's coordinates,
    // which start at the top-left of all monitors together, not at the primary monitor.
    MONITORINFO monitor{};
    monitor.cbSize = sizeof monitor;
    ::GetMonitorInfoW(::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor);
    RECT area = monitor.rcMonitor;
    // A RECT is two POINTs in a row (left/top, right/bottom), so it can be converted as such.
    ::MapWindowPoints(HWND_DESKTOP, parent, reinterpret_cast<POINT*>(&area), 2);

    HWND insert_after = plan->insert_after != 0 ? to_hwnd(plan->insert_after) : HWND_TOP;
    ::SetWindowPos(window, insert_after, area.left, area.top, area.right - area.left,
                   area.bottom - area.top, SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_FRAMECHANGED);
    return plan->layout;
}

void repaint(HWND parent) {
    if (parent != nullptr) {
        ::RedrawWindow(parent, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    }
}

}  // namespace wallflow::win
