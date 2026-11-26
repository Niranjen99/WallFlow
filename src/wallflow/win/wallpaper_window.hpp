#pragma once

#include <windows.h>

#include <optional>

#include "wallflow/desktop_layout.hpp"
#include "wallflow/win/window_proc.hpp"

namespace wallflow::win {

// The window that sits behind the desktop icons. For now it only paints a solid colour;
// the video player draws into it from Step 2 on.
//
// Owns its HWND: the destructor destroys the window. Not copyable or movable, because
// Windows holds a pointer to this object (see window_proc.hpp).
class WallpaperWindow {
public:
    explicit WallpaperWindow(HINSTANCE instance);
    ~WallpaperWindow();

    WallpaperWindow(const WallpaperWindow&) = delete;
    WallpaperWindow& operator=(const WallpaperWindow&) = delete;

    // Moves the window behind the desktop icons. Returns nothing if the desktop is not ready.
    std::optional<DesktopLayout> attach();

    // False once the window is gone, e.g. because Explorer restarted and took its parent
    // (and with it our window) down.
    [[nodiscard]] bool alive() const noexcept { return hwnd_ != nullptr; }

private:
    template <typename T>
    friend LRESULT CALLBACK window_proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT on_message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    HWND hwnd_ = nullptr;
};

}  // namespace wallflow::win
