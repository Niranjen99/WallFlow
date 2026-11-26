#pragma once

#include <windows.h>

#include <optional>
#include <vector>

#include "wallflow/desktop_layout.hpp"

namespace wallflow::win {

// Reads the desktop's window tree in the shape plan_attach() expects: every top-level window
// in z-order, followed by the direct children of each Progman and WorkerW window.
std::vector<WindowInfo> snapshot_desktop();

// Puts `window` behind the desktop icons, covering the primary monitor. Asks Explorer to
// create the wallpaper layer first if needed. Returns the layout used, or nothing if the
// desktop is not ready yet (the caller should try again a little later).
std::optional<DesktopLayout> attach_behind_icons(HWND window);

// Asks the window we were attached to (and its children) to repaint, so no leftover frame
// of our window stays visible after it is gone.
void repaint(HWND parent);

}  // namespace wallflow::win
