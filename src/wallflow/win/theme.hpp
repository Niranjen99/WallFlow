#pragma once

#include <windows.h>

namespace wallflow::win {

// Icon resource id of the app icon (see src/wallflow.rc.in).
inline constexpr int kAppIconId = 1;

// Makes this app's menus (the tray menu) follow the Windows app theme, light or dark.
//
// Windows has no documented API for dark Win32 menus. uxtheme.dll exports two functions only
// by number, which File Explorer and many apps use: #135 SetPreferredAppMode (Windows 10 1903
// and later; on 1809 the same number is AllowDarkModeForApp(bool), which takes the same value)
// and #136 FlushMenuThemes. If they are missing (older Windows, or Microsoft removes them),
// this does nothing and menus stay light.
void follow_system_theme_for_menus();

// Call on WM_SETTINGCHANGE with lParam "ImmersiveColorSet" (the user switched light/dark):
// menus pick up the new theme the next time they open.
void refresh_menu_theme();

// True if the lParam of a WM_SETTINGCHANGE says the colour theme changed.
bool is_theme_change(LPARAM lparam);

// True if Windows is set to dark mode for apps (Settings > Personalization > Colors).
bool apps_use_dark_theme();

// Dark or light title bar for `window` (documented: DWMWA_USE_IMMERSIVE_DARK_MODE, Windows 11
// and Windows 10 20H1+). Windows does not do this by itself for Win32 windows.
void set_dark_title_bar(HWND window, bool dark);

}  // namespace wallflow::win
