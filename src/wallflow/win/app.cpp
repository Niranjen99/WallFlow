#include "wallflow/win/app.hpp"

#include <wtsapi32.h>

#include <cstring>
#include <stdexcept>
#include <string>

#include "wallflow/coverage.hpp"
#include "wallflow/win/log.hpp"

namespace wallflow::win {

namespace {

constexpr wchar_t kClassName[] = L"WallFlow.Controller";
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kPlayerMessage = WM_APP + 2;  // mpv has events waiting
constexpr UINT_PTR kAttachTimer = 1;
constexpr UINT kAttachRetryMs = 500;
constexpr int kMaxAttachAttempts = 20;  // 10 seconds
constexpr UINT_PTR kCoverageTimer = 2;
// Window events come in bursts (dragging a window sends dozens). The check runs once, this
// long after the last event of a burst.
constexpr UINT kCoverageDelayMs = 200;

Rect primary_monitor() {
    MONITORINFO info{};
    info.cbSize = sizeof info;
    ::GetMonitorInfoW(::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &info);
    return {info.rcMonitor.left, info.rcMonitor.top, info.rcMonitor.right, info.rcMonitor.bottom};
}

}  // namespace

App::App(HINSTANCE instance, CommandLine start) : instance_(instance), start_(std::move(start)) {
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

    // Lock and unlock arrive as WM_WTSSESSION_CHANGE; display on/off as WM_POWERBROADCAST.
    // Both are notifications Windows pushes to us: nothing is polled.
    ::WTSRegisterSessionNotification(hwnd_, NOTIFY_FOR_THIS_SESSION);
    display_notify_ = ::RegisterPowerSettingNotification(hwnd_, &GUID_CONSOLE_DISPLAY_STATE,
                                                         DEVICE_NOTIFY_WINDOW_HANDLE);
    events_ = std::make_unique<WindowEvents>(hwnd_, kCoverageTimer, kCoverageDelayMs);

    check_coverage();  // something may already be maximized when WallFlow starts
    start_wallpaper();
}

App::~App() {
    // The controller window is a raw HWND, so it is destroyed by hand, after everything that
    // still uses it.
    engine_.reset();
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
    // After an Explorer restart the old windows are usually already gone.
    engine_.reset();
    engine_ = std::make_unique<VideoEngine>(instance_, hwnd_, kPlayerMessage);
    engine_playing_ = false;
    attach_attempts_ = 0;
    try_attach();
}

void App::try_attach() {
    ++attach_attempts_;
    if (const auto layout = engine_->attach()) {
        ::KillTimer(hwnd_, kAttachTimer);
        log(std::wstring(L"attached behind the icons (") +
            (*layout == DesktopLayout::Modern ? L"modern" : L"classic") + L" layout), attempt " +
            std::to_wstring(attach_attempts_));
        start_player();
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

void App::start_player() {
    if (start_.video.empty() || engine_playing_) {
        return;
    }
    try {
        // If it should be paused right now (e.g. Explorer restarted while a game runs), it
        // loads paused: the first frame is shown, nothing more is decoded.
        paused_ = covered_ || locked_ || display_off_;
        engine_->play(start_.video, start_.loop, paused_);
        engine_playing_ = true;
        log(L"loading " + start_.video.wstring());
    } catch (const std::exception& e) {
        // Keep the engine (its windows stay invisible): an Explorer restart tries again.
        log(L"player failed: " + to_wide(e.what()));
    }
}

void App::check_coverage() {
    const bool covered = is_monitor_covered(primary_monitor(), snapshot_windows());
    if (covered != covered_) {
        covered_ = covered;
        log(covered ? L"monitor covered by a window" : L"monitor uncovered");
        update_pause();
    }
}

void App::update_pause() {
    const bool pause = covered_ || locked_ || display_off_;
    if (engine_playing_ && pause != paused_) {
        engine_->set_paused(pause);
        paused_ = pause;
        log(pause ? L"paused" : L"playing");
    }
}

void App::quit() {
    // Remove the wallpaper and the tray icon while the controller window still exists (the
    // tray icon is identified by its owner window), then end the message loop.
    ::KillTimer(hwnd_, kAttachTimer);
    events_.reset();
    if (display_notify_ != nullptr) {
        ::UnregisterPowerSettingNotification(display_notify_);
        display_notify_ = nullptr;
    }
    ::WTSUnRegisterSessionNotification(hwnd_);
    engine_.reset();
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

        case kPlayerMessage:
            if (engine_) {
                engine_->on_player_message();
            }
            return 0;

        case WM_TIMER:
            if (wparam == kAttachTimer) {
                try_attach();
            } else if (wparam == kCoverageTimer) {
                ::KillTimer(hwnd, kCoverageTimer);  // timers repeat; this one should fire once
                check_coverage();
            }
            return 0;

        case WM_WTSSESSION_CHANGE:
            if (wparam == WTS_SESSION_LOCK || wparam == WTS_SESSION_UNLOCK) {
                locked_ = wparam == WTS_SESSION_LOCK;
                log(locked_ ? L"session locked" : L"session unlocked");
                update_pause();
            }
            return 0;

        case WM_POWERBROADCAST:
            if (wparam == PBT_POWERSETTINGCHANGE) {
                // lparam points at a block Windows owns, valid only during this message.
                const auto* setting = reinterpret_cast<const POWERBROADCAST_SETTING*>(lparam);
                if (setting->PowerSetting == GUID_CONSOLE_DISPLAY_STATE &&
                    setting->DataLength == sizeof(DWORD)) {
                    // Data is a DWORD: 0 = off, 1 = on, 2 = dimmed (still visible).
                    DWORD state = 0;
                    std::memcpy(&state, setting->Data, sizeof state);
                    display_off_ = state == 0;
                    log(display_off_ ? L"display off" : L"display on");
                    update_pause();
                }
                return TRUE;
            }
            break;

        case WM_DISPLAYCHANGE:
            // Resolution or monitor layout changed: fit the primary monitor again.
            if (engine_ && engine_->alive()) {
                engine_->attach();
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
