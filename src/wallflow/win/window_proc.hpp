#pragma once

#include <windows.h>

namespace wallflow::win {

// Windows delivers every message for a window to one plain function (the "window
// procedure"). It cannot call a member function directly, so this template connects the two:
//
// 1. CreateWindowExW receives `this` as its last argument (lpCreateParams).
// 2. On WM_NCCREATE, the first message that carries it, we store that pointer in the
//    window's GWLP_USERDATA slot.
// 3. For every later message we read the pointer back and call T::on_message.
//
// T must declare: LRESULT on_message(HWND, UINT, WPARAM, LPARAM), and make this template
// a friend if on_message is private.
template <typename T>
LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    T* self = nullptr;
    if (msg == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        self = static_cast<T*>(create->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<T*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    // A few messages (e.g. WM_GETMINMAXINFO) arrive before WM_NCCREATE, and none should be
    // forwarded after WM_NCDESTROY, when the object may already be gone.
    if (self == nullptr) {
        return ::DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    const LRESULT result = self->on_message(hwnd, msg, wparam, lparam);
    if (msg == WM_NCDESTROY) {
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    }
    return result;
}

}  // namespace wallflow::win
