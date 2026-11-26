#pragma once

#include <windows.h>

#include <optional>

#include "wallflow/desktop_layout.hpp"
#include "wallflow/win/window_proc.hpp"

namespace wallflow::win {

// The window that sits behind the desktop icons; the video player draws into it.
//
// It starts fully transparent, so the normal wallpaper stays visible until reveal() is called
// once the video's first frame is ready. That way there is never a flash of an empty window.
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

    [[nodiscard]] HWND hwnd() const noexcept { return hwnd_; }

    // Makes the window opaque. Safe to call repeatedly.
    void reveal();
    [[nodiscard]] bool revealed() const noexcept { return revealed_; }

private:
    template <typename T>
    friend LRESULT CALLBACK window_proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT on_message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    HWND hwnd_ = nullptr;
    bool revealed_ = false;
};

}  // namespace wallflow::win
