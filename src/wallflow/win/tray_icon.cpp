#include "wallflow/win/tray_icon.hpp"

#include <cwchar>
#include <memory>
#include <type_traits>

#include "wallflow/win/theme.hpp"

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
    // The app icon from the exe's resources, at the small-icon size for this screen (the
    // .ico has 16 to 40 px versions; Windows picks the closest). In the unit tests the
    // resource is not there (it belongs to wallflow.exe), so fall back to Windows' own.
    icon_ = static_cast<HICON>(::LoadImageW(::GetModuleHandleW(nullptr), MAKEINTRESOURCEW(kAppIconId),
                                            IMAGE_ICON, ::GetSystemMetrics(SM_CXSMICON),
                                            ::GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    data_.hIcon = icon_ != nullptr ? icon_ : ::LoadIconW(nullptr, IDI_APPLICATION);
    ::wcscpy_s(data_.szTip, L"WallFlow");
}

TrayIcon::~TrayIcon() {
    if (shown_) {
        ::Shell_NotifyIconW(NIM_DELETE, &data_);
    }
    if (icon_ != nullptr) {
        ::DestroyIcon(icon_);  // loaded without LR_SHARED, so it is ours to free
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
