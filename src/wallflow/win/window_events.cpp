#include "wallflow/win/window_events.hpp"

#include <dwmapi.h>

#include <array>
#include <stdexcept>

namespace wallflow::win {

namespace {

// WinEvent callbacks receive no user pointer (unlike window procedures, EnumWindows or mpv),
// so the one active instance is reached through this variable. thread_local: it belongs to
// the UI thread, which is also the only thread the callbacks run on (WINEVENT_OUTOFCONTEXT
// delivers them through that thread's message loop).
thread_local WindowEvents* t_instance = nullptr;

constexpr DWORD kFlags = WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS;

struct EventRange {
    DWORD first;
    DWORD last;
};

// Watched for every process. EVENT_OBJECT_LOCATIONCHANGE is deliberately NOT here: it is also
// raised for the mouse cursor, so a global hook would wake us on every mouse movement. It is
// hooked only for the foreground window's process instead (watch_moves_of).
constexpr std::array<EventRange, 5> kGlobalEvents{{
    {EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND},
    {EVENT_SYSTEM_MOVESIZEEND, EVENT_SYSTEM_MOVESIZEEND},
    {EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND},
    {EVENT_OBJECT_SHOW, EVENT_OBJECT_HIDE},
    {EVENT_OBJECT_CLOAKED, EVENT_OBJECT_UNCLOAKED},
}};

bool is_top_level(HWND hwnd) {
    return ::GetAncestor(hwnd, GA_PARENT) == ::GetDesktopWindow();
}

}  // namespace

WindowEvents::WindowEvents(HWND window, UINT_PTR timer_id, UINT delay_ms)
    : window_(window), timer_id_(timer_id), delay_ms_(delay_ms) {
    if (t_instance != nullptr) {
        throw std::logic_error("only one WindowEvents may exist at a time");
    }
    t_instance = this;
    for (const auto& range : kGlobalEvents) {
        if (HWINEVENTHOOK hook = ::SetWinEventHook(range.first, range.last, nullptr, on_event, 0, 0, kFlags)) {
            hooks_.push_back(hook);
        }
    }
    watch_moves_of(::GetForegroundWindow());
}

WindowEvents::~WindowEvents() {
    for (HWINEVENTHOOK hook : hooks_) {
        ::UnhookWinEvent(hook);
    }
    if (move_hook_ != nullptr) {
        ::UnhookWinEvent(move_hook_);
    }
    ::KillTimer(window_, timer_id_);
    t_instance = nullptr;
}

void WindowEvents::watch_moves_of(HWND foreground) {
    DWORD process = 0;
    if (foreground != nullptr) {
        ::GetWindowThreadProcessId(foreground, &process);
    }
    if (process == move_process_) {
        return;  // already watching this process
    }
    if (move_hook_ != nullptr) {
        ::UnhookWinEvent(move_hook_);
        move_hook_ = nullptr;
    }
    move_process_ = process;
    if (process != 0) {
        move_hook_ = ::SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE,
                                       nullptr, on_event, process, 0, kFlags);
    }
}

void CALLBACK WindowEvents::on_event(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG id_object,
                                     LONG id_child, DWORD, DWORD) {
    WindowEvents* self = t_instance;
    // Only events about whole top-level windows: not the caret, scroll bars, menu items or
    // child windows (a browser shows and hides child windows all the time).
    if (self == nullptr || hwnd == nullptr || id_object != OBJID_WINDOW ||
        id_child != CHILDID_SELF || !is_top_level(hwnd)) {
        return;
    }
    if (event == EVENT_SYSTEM_FOREGROUND) {
        self->watch_moves_of(hwnd);
    }
    // SetTimer with an existing id restarts it: this is the debounce.
    ::SetTimer(self->window_, self->timer_id_, self->delay_ms_, nullptr);
}

std::vector<WindowState> snapshot_windows() {
    std::vector<HWND> handles;
    ::EnumWindows(
        [](HWND hwnd, LPARAM lparam) -> BOOL {
            reinterpret_cast<std::vector<HWND>*>(lparam)->push_back(hwnd);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&handles));

    std::vector<WindowState> windows;
    for (HWND hwnd : handles) {
        if (!::IsWindowVisible(hwnd)) {
            continue;  // most top-level windows are hidden helper windows; skip them early
        }
        WindowState w;
        RECT r{};
        ::GetWindowRect(hwnd, &r);
        w.rect = {r.left, r.top, r.right, r.bottom};

        std::array<wchar_t, 256> name{};
        const int length = ::GetClassNameW(hwnd, name.data(), static_cast<int>(name.size()));
        w.class_name.assign(name.data(), length > 0 ? static_cast<std::size_t>(length) : 0);

        w.visible = true;
        w.minimized = ::IsIconic(hwnd) != FALSE;
        w.maximized = ::IsZoomed(hwnd) != FALSE;
        DWORD cloaked = 0;
        w.cloaked = SUCCEEDED(::DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof cloaked)) &&
                    cloaked != 0;
        w.click_through = (::GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TRANSPARENT) != 0;
        windows.push_back(std::move(w));
    }
    return windows;
}

}  // namespace wallflow::win
