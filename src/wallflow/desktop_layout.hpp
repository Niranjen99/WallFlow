#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace wallflow {

// One window in a snapshot of the desktop. Handles are stored as plain numbers so the
// decision below can be unit-tested with made-up window trees, without real windows.
struct WindowInfo {
    std::uintptr_t handle = 0;
    std::uintptr_t parent = 0;  // 0 for a top-level window
    std::wstring class_name;
};

enum class DesktopLayout {
    // Before Windows 11 24H2: the icons live in a top-level WorkerW window, and the
    // wallpaper layer is the next top-level WorkerW below it.
    Classic,
    // Windows 11 24H2 and later: the icons (SHELLDLL_DefView) and the wallpaper layer
    // (WorkerW) are both children of Progman.
    Modern,
};

struct AttachPlan {
    DesktopLayout layout = DesktopLayout::Modern;
    std::uintptr_t parent = 0;        // make our window a child of this window
    std::uintptr_t insert_after = 0;  // put ours directly below this sibling; 0 = on top
};

// `windows` lists top-level windows and the direct children of Progman and WorkerW windows.
// Top-level windows must be in z-order (topmost first), the order EnumWindows reports.
// Returns where a wallpaper window has to go to appear behind the desktop icons, or nothing
// if the desktop is not ready (for example, Explorer has not created the wallpaper layer).
std::optional<AttachPlan> plan_attach(std::span<const WindowInfo> windows);

}  // namespace wallflow
