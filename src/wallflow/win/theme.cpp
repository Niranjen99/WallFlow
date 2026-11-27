#include "wallflow/win/theme.hpp"

#include <cwchar>

namespace wallflow::win {

namespace {

// The value #135 takes. 1 = "AllowDark": dark when the system is dark, light otherwise.
// (On Windows 10 1809 the function takes a BOOL instead, and TRUE is also 1.)
enum class PreferredAppMode : int { Default = 0, AllowDark = 1 };

using SetPreferredAppModeFn = PreferredAppMode(WINAPI*)(PreferredAppMode);
using FlushMenuThemesFn = void(WINAPI*)();

HMODULE uxtheme() {
    // LOAD_LIBRARY_SEARCH_SYSTEM32: only ever the real one from System32. uxtheme.dll is
    // loaded in every GUI process anyway, so this does not cost anything; it is never freed.
    static HMODULE module = ::LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    return module;
}

// GetProcAddress by number: MAKEINTRESOURCEA turns the number into the "name" pointer, the
// documented way to look up an export that has no name. The result is a generic FARPROC; it
// must be cast to the function's real type before calling, and calling it with the wrong type
// would be undefined behaviour, which is why the types above match the known signatures.
template <typename Fn>
Fn by_ordinal(int ordinal) {
    if (HMODULE module = uxtheme()) {
        return reinterpret_cast<Fn>(::GetProcAddress(module, MAKEINTRESOURCEA(ordinal)));
    }
    return nullptr;
}

}  // namespace

void follow_system_theme_for_menus() {
    if (const auto set_mode = by_ordinal<SetPreferredAppModeFn>(135)) {
        set_mode(PreferredAppMode::AllowDark);
        refresh_menu_theme();
    }
}

void refresh_menu_theme() {
    if (const auto flush = by_ordinal<FlushMenuThemesFn>(136)) {
        flush();
    }
}

bool is_theme_change(LPARAM lparam) {
    // lParam points at a string naming the changed area; the theme's is "ImmersiveColorSet".
    const auto* area = reinterpret_cast<const wchar_t*>(lparam);
    return area != nullptr && std::wcscmp(area, L"ImmersiveColorSet") == 0;
}

bool apps_use_dark_theme() {
    // Where the Settings app stores the choice (not formally documented, but what apps
    // commonly read): 0 = dark, 1 = light. Missing (very old Windows) = light.
    DWORD light = 1;
    DWORD bytes = sizeof light;
    ::RegGetValueW(HKEY_CURRENT_USER,
                   L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                   L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &bytes);
    return light == 0;
}

}  // namespace wallflow::win
