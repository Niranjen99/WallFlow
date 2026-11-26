#include "wallflow/win/wallpaper_window.hpp"

#include <stdexcept>

#include "wallflow/win/desktop.hpp"

namespace wallflow::win {

namespace {

constexpr wchar_t kClassName[] = L"WallFlow.Wallpaper";
constexpr COLORREF kColour = RGB(0x1E, 0x4D, 0x6B);  // placeholder until video arrives

void register_class(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = window_proc<WallpaperWindow>;
    wc.hInstance = instance;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    // Registering twice fails with ERROR_CLASS_ALREADY_EXISTS, which is fine: after an
    // Explorer restart we create a second window of the same class.
    if (::RegisterClassExW(&wc) == 0 && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        throw std::runtime_error("RegisterClassExW failed for the wallpaper window");
    }
}

}  // namespace

WallpaperWindow::WallpaperWindow(HINSTANCE instance) {
    register_class(instance);
    // Created as a hidden pop-up; attach() turns it into a child of the desktop.
    // WS_EX_NOACTIVATE: clicking it never takes focus away from the desktop or other apps.
    // WS_EX_LAYERED: since Windows 11 24H2, Progman has WS_EX_NOREDIRECTIONBITMAP, so it has
    // no off-screen surface for ordinary child windows to draw into, and what they paint never
    // reaches the screen. A layered window gets its own surface, which the compositor (DWM)
    // shows directly. The icon window (SHELLDLL_DefView) is layered for the same reason.
    ::CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_LAYERED, kClassName,
                      L"WallFlow wallpaper", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance,
                      this);
    if (hwnd_ == nullptr) {  // set by on_message during creation
        throw std::runtime_error("CreateWindowExW failed for the wallpaper window");
    }
    // A layered window stays invisible until its opacity is set. 255 = fully opaque.
    ::SetLayeredWindowAttributes(hwnd_, 0, 255, LWA_ALPHA);
}

WallpaperWindow::~WallpaperWindow() {
    if (hwnd_ != nullptr) {
        HWND parent = ::GetAncestor(hwnd_, GA_PARENT);
        ::DestroyWindow(hwnd_);  // WM_NCDESTROY sets hwnd_ back to nullptr
        repaint(parent);
    }
}

std::optional<DesktopLayout> WallpaperWindow::attach() {
    return attach_behind_icons(hwnd_);
}

LRESULT WallpaperWindow::on_message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_NCCREATE:
            hwnd_ = hwnd;
            break;

        case WM_ERASEBKGND:
            return 1;  // WM_PAINT fills everything; skipping the erase avoids a flicker

        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = ::BeginPaint(hwnd, &ps);
            // DC_BRUSH is a stock brush whose colour is set per device context, so no brush
            // has to be created and deleted.
            ::SetDCBrushColor(dc, kColour);
            ::FillRect(dc, &ps.rcPaint, static_cast<HBRUSH>(::GetStockObject(DC_BRUSH)));
            ::EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_NCDESTROY:
            hwnd_ = nullptr;  // the window is gone, whoever destroyed it
            break;

        default:
            break;
    }
    return ::DefWindowProcW(hwnd, msg, wparam, lparam);
}

}  // namespace wallflow::win
