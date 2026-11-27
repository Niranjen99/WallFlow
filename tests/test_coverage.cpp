#include <vector>

#include <gtest/gtest.h>

#include "wallflow/coverage.hpp"

using wallflow::is_monitor_covered;
using wallflow::Rect;
using wallflow::WindowState;

namespace {

const Rect kMonitor{0, 0, 1920, 1080};

WindowState window(Rect rect, const wchar_t* class_name) {
    WindowState w;
    w.rect = rect;
    w.class_name = class_name;
    return w;
}

}  // namespace

// The first four tests are the real windows found on the development machine on 2026-11-27.

TEST(Coverage, MaximizedBrowserCovers) {
    auto chrome = window({-8, -8, 1928, 1040}, L"Chrome_WidgetWin_1");  // taskbar still visible
    chrome.maximized = true;
    EXPECT_TRUE(is_monitor_covered(kMonitor, std::vector{chrome}));
}

TEST(Coverage, CloakedMaximizedAppDoesNotCover) {
    // The Settings app, suspended in the background: maximized and "visible", but cloaked.
    auto settings = window({-8, -8, 1928, 1040}, L"ApplicationFrameWindow");
    settings.maximized = true;
    settings.cloaked = true;
    EXPECT_FALSE(is_monitor_covered(kMonitor, std::vector{settings}));
}

TEST(Coverage, CloakedFullScreenSystemWindowDoesNotCover) {
    // "Windows Input Experience": exactly the size of the monitor, but cloaked.
    auto input = window(kMonitor, L"Windows.UI.Core.CoreWindow");
    input.cloaked = true;
    EXPECT_FALSE(is_monitor_covered(kMonitor, std::vector{input}));
}

TEST(Coverage, TheDesktopItselfDoesNotCover) {
    EXPECT_FALSE(is_monitor_covered(kMonitor, std::vector{window(kMonitor, L"Progman")}));
}

TEST(Coverage, FullScreenGameCovers) {
    // Borderless or exclusive full screen: exactly the monitor's size, not "maximized".
    EXPECT_TRUE(is_monitor_covered(kMonitor, std::vector{window(kMonitor, L"UnrealWindow")}));
}

TEST(Coverage, NormalWindowDoesNotCover) {
    EXPECT_FALSE(is_monitor_covered(kMonitor, std::vector{window({100, 100, 1200, 800}, L"Notepad")}));
}

TEST(Coverage, AlmostFullScreenWindowDoesNotCover) {
    // One pixel short on the right: the wallpaper is (barely) visible, so it keeps playing.
    EXPECT_FALSE(is_monitor_covered(kMonitor, std::vector{window({0, 0, 1919, 1080}, L"App")}));
}

TEST(Coverage, MinimizedOrHiddenWindowsDoNotCover) {
    auto minimized = window({-8, -8, 1928, 1040}, L"Chrome_WidgetWin_1");
    minimized.maximized = true;  // Windows remembers "maximized" while minimized
    minimized.minimized = true;
    auto hidden = window(kMonitor, L"Hidden");
    hidden.visible = false;
    EXPECT_FALSE(is_monitor_covered(kMonitor, std::vector{minimized, hidden}));
}

TEST(Coverage, ClickThroughOverlayDoesNotCover) {
    // Overlays (FPS counters, recording indicators) often span the whole screen but let every
    // click through to what is below. They do not hide the wallpaper on their own.
    auto overlay = window(kMonitor, L"SomeOverlay");
    overlay.click_through = true;
    EXPECT_FALSE(is_monitor_covered(kMonitor, std::vector{overlay}));
}

TEST(Coverage, FullyTransparentOverlayDoesNotCover) {
    // Found on a real PC: the NVIDIA GeForce Overlay keeps a screen-sized layered window open
    // at opacity 0 (not click-through), which paused the wallpaper forever.
    auto overlay = window(kMonitor, L"CEF-OSC-WIDGET");
    overlay.see_through = true;
    EXPECT_FALSE(is_monitor_covered(kMonitor, std::vector{overlay}));
    overlay.see_through = false;  // opened with Alt+Z: now it really covers the screen
    EXPECT_TRUE(is_monitor_covered(kMonitor, std::vector{overlay}));
}

TEST(Coverage, MaximizedOnAnotherMonitorDoesNotCover) {
    // A second monitor to the right of the primary one.
    auto other = window({1912, -8, 3848, 1040}, L"Chrome_WidgetWin_1");
    other.maximized = true;
    EXPECT_FALSE(is_monitor_covered(kMonitor, std::vector{other}));
}

TEST(Coverage, FullScreenOnAnotherMonitorDoesNotCover) {
    EXPECT_FALSE(is_monitor_covered(kMonitor, std::vector{window({1920, 0, 3840, 1080}, L"Game")}));
}

TEST(Coverage, OneCoveringWindowAmongManyIsEnough) {
    auto game = window(kMonitor, L"Game");
    EXPECT_TRUE(is_monitor_covered(
        kMonitor, std::vector{window(kMonitor, L"Progman"), window({0, 0, 300, 300}, L"Notepad"), game}));
}

TEST(Coverage, NoWindowsAtAll) {
    EXPECT_FALSE(is_monitor_covered(kMonitor, std::vector<WindowState>{}));
}
