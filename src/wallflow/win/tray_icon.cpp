#include "wallflow/win/tray_icon.hpp"

#include <cwchar>
#include <memory>
#include <type_traits>

namespace wallflow::win {

namespace {

// HMENU is a pointer type (HMENU__*), so std::unique_ptr can own one if it is told how to
// free it: the element type is what HMENU points to, the deleter is DestroyMenu.
using UniqueMenu = std::unique_ptr<std::remove_pointer_t<HMENU>, decltype(&::DestroyMenu)>;

}  // namespace

TrayIcon::TrayIcon(HWND owner, UINT callback_message) {
    data_.cbSize = sizeof data_;
    data_.hWnd = owner;
    data_.uID = 1;
    data_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    data_.uCallbackMessage = callback_message;
    data_.hIcon = ::LoadIconW(nullptr, IDI_APPLICATION);  // a real icon comes later
    ::wcscpy_s(data_.szTip, L"WallFlow");
}

TrayIcon::~TrayIcon() {
    if (shown_) {
        ::Shell_NotifyIconW(NIM_DELETE, &data_);
    }
}

bool TrayIcon::show() {
    shown_ = ::Shell_NotifyIconW(NIM_ADD, &data_) != FALSE;
    return shown_;
}

UINT TrayIcon::show_menu() {
    UniqueMenu menu(::CreatePopupMenu(), &::DestroyMenu);
    if (!menu) {
        return 0;
    }
    ::AppendMenuW(menu.get(), MF_STRING, kEditSettings, L"Edit settings...");
    ::AppendMenuW(menu.get(), MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu.get(), MF_STRING, kQuit, L"Quit WallFlow");

    POINT cursor{};
    ::GetCursorPos(&cursor);
    // Both calls around TrackPopupMenu are required by its documentation: without the first,
    // the menu does not close when you click elsewhere; the second makes the next click work.
    ::SetForegroundWindow(data_.hWnd);
    const auto chosen = static_cast<UINT>(::TrackPopupMenu(
        menu.get(), TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, cursor.x, cursor.y, 0,
        data_.hWnd, nullptr));
    ::PostMessageW(data_.hWnd, WM_NULL, 0, 0);
    return chosen;
}

}  // namespace wallflow::win
