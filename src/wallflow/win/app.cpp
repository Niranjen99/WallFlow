#include "wallflow/win/app.hpp"

#include <stdexcept>
#include <string>

namespace wallflow::win {

namespace {

constexpr wchar_t kClassName[] = L"WallFlow.Controller";
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT_PTR kAttachTimer = 1;
constexpr UINT kAttachRetryMs = 500;
constexpr int kMaxAttachAttempts = 20;  // 10 seconds

void log(const std::wstring& text) {
    // Visible in Visual Studio's Output window or Sysinternals DebugView.
    ::OutputDebugStringW((L"WallFlow: " + text + L"\n").c_str());
}

}  // namespace

App::App(HINSTANCE instance) : instance_(instance) {
    // Explorer broadcasts this message to all top-level windows whenever the taskbar is
    // (re)created, which is how apps learn that Explorer restarted.
    taskbar_created_ = ::RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = window_proc<App>;
    wc.hInstance = instance;
    wc.lpszClassName = kClassName;
    if (::RegisterClassExW(&wc) == 0) {
        throw std::runtime_error("RegisterClassExW failed for the controller window");
    }
    // Never shown. WS_EX_TOOLWINDOW keeps it out of the taskbar and Alt+Tab regardless.
    ::CreateWindowExW(WS_EX_TOOLWINDOW, kClassName, L"WallFlow", WS_OVERLAPPED, 0, 0, 0, 0,
                      nullptr, nullptr, instance, this);
    if (hwnd_ == nullptr) {
        throw std::runtime_error("CreateWindowExW failed for the controller window");
    }

    tray_ = std::make_unique<TrayIcon>(hwnd_, kTrayMessage);
    tray_->show();
    start_wallpaper();
}

App::~App() {
    // Members are destroyed in reverse order of declaration (wallpaper_, then tray_), but
    // the controller window is a raw HWND, so it is destroyed by hand, last.
    wallpaper_.reset();
    tray_.reset();
    if (hwnd_ != nullptr) {
        ::DestroyWindow(hwnd_);
    }
}

int App::run() {
    MSG msg{};
    // GetMessage waits without using CPU until a message arrives. It returns 0 for WM_QUIT
    // and -1 on error.
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

void App::start_wallpaper() {
    ::KillTimer(hwnd_, kAttachTimer);
    wallpaper_.reset();  // after an Explorer restart the old window is usually already gone
    wallpaper_ = std::make_unique<WallpaperWindow>(instance_);
    attach_attempts_ = 0;
    try_attach();
}

void App::try_attach() {
    ++attach_attempts_;
    if (const auto layout = wallpaper_->attach()) {
        ::KillTimer(hwnd_, kAttachTimer);
        log(std::wstring(L"attached behind the icons (") +
            (*layout == DesktopLayout::Modern ? L"modern" : L"classic") + L" layout), attempt " +
            std::to_wstring(attach_attempts_));
        return;
    }
    if (attach_attempts_ >= kMaxAttachAttempts) {
        ::KillTimer(hwnd_, kAttachTimer);
        log(L"desktop not found, giving up");
        ::MessageBoxW(nullptr, L"WallFlow could not find the desktop to draw on.", L"WallFlow",
                      MB_OK | MB_ICONWARNING);
        return;
    }
    // SetTimer with the same id replaces the existing timer, so calling this again is safe.
    ::SetTimer(hwnd_, kAttachTimer, kAttachRetryMs, nullptr);
}

void App::quit() {
    // Remove the wallpaper and the tray icon while the controller window still exists (the
    // tray icon is identified by its owner window), then end the message loop.
    ::KillTimer(hwnd_, kAttachTimer);
    wallpaper_.reset();
    tray_.reset();
    ::DestroyWindow(hwnd_);  // WM_DESTROY posts WM_QUIT
}

LRESULT App::on_message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    // RegisterWindowMessage numbers are only known at run time, so this cannot be a case label.
    if (msg == taskbar_created_ && taskbar_created_ != 0) {
        log(L"Explorer restarted: re-adding the tray icon and re-attaching");
        tray_->show();
        start_wallpaper();
        return 0;
    }

    switch (msg) {
        case WM_NCCREATE:
            hwnd_ = hwnd;
            break;

        case kTrayMessage:
            if (LOWORD(lparam) == WM_RBUTTONUP && tray_->show_menu() == TrayIcon::kQuit) {
                quit();
            }
            return 0;

        case WM_TIMER:
            if (wparam == kAttachTimer) {
                try_attach();
            }
            return 0;

        case WM_DISPLAYCHANGE:
            // Resolution or monitor layout changed: fit the primary monitor again.
            if (wallpaper_ && wallpaper_->alive()) {
                wallpaper_->attach();
            }
            return 0;

        case WM_CLOSE:
            // Someone asked us to close (Windows shutting down, an installer, a script).
            // Clean up exactly like the tray menu's Quit.
            quit();
            return 0;

        case WM_DESTROY:
            ::PostQuitMessage(0);
            return 0;

        case WM_NCDESTROY:
            hwnd_ = nullptr;
            break;

        default:
            break;
    }
    return ::DefWindowProcW(hwnd, msg, wparam, lparam);
}

}  // namespace wallflow::win
