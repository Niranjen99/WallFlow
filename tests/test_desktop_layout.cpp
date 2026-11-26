#include <vector>

#include <gtest/gtest.h>

#include "wallflow/desktop_layout.hpp"

using wallflow::AttachPlan;
using wallflow::DesktopLayout;
using wallflow::plan_attach;
using wallflow::WindowInfo;

// Window trees are listed top-level first in z-order (topmost first), with each window's
// direct children after it, the way the real snapshot code builds them.

TEST(DesktopLayout, ModernLayoutFromARealSnapshot) {
    // Shape of this machine's desktop on 2026-11-27 (Windows build 26300), with Lively
    // Wallpaper's mpv window already attached. Handles shortened.
    const std::vector<WindowInfo> windows{
        {0x538, 0, L"WorkerW"},   // unrelated hidden WorkerW windows (Explorer, RuntimeBroker...)
        {0x5E8, 0, L"WorkerW"},
        {0x1B8, 0, L"Progman"},
        {0x1C2, 0x1B8, L"SHELLDLL_DefView"},
        {0xC70, 0x1B8, L"mpv"},
        {0x200, 0x1B8, L"WorkerW"},
    };
    const auto plan = plan_attach(windows);
    ASSERT_TRUE(plan.has_value());
    EXPECT_EQ(plan->layout, DesktopLayout::Modern);
    EXPECT_EQ(plan->parent, 0x1B8u);        // child of Progman
    EXPECT_EQ(plan->insert_after, 0x1C2u);  // directly below the icons
}

TEST(DesktopLayout, ClassicLayoutUsesTheWorkerWBehindTheIcons) {
    const std::vector<WindowInfo> windows{
        {0x10, 0, L"WorkerW"},                  // an unrelated WorkerW above the icons
        {0x20, 0, L"WorkerW"},                  // holds the icons
        {0x21, 0x20, L"SHELLDLL_DefView"},
        {0x30, 0, L"Notepad"},
        {0x40, 0, L"WorkerW"},                  // the wallpaper layer
        {0x50, 0, L"WorkerW"},
        {0x60, 0, L"Progman"},
    };
    const auto plan = plan_attach(windows);
    ASSERT_TRUE(plan.has_value());
    EXPECT_EQ(plan->layout, DesktopLayout::Classic);
    EXPECT_EQ(plan->parent, 0x40u);
    EXPECT_EQ(plan->insert_after, 0u);
}

TEST(DesktopLayout, ClassicWithoutAWallpaperLayerIsNotReady) {
    const std::vector<WindowInfo> windows{
        {0x60, 0, L"Progman"},
        {0x20, 0, L"WorkerW"},
        {0x21, 0x20, L"SHELLDLL_DefView"},
    };
    EXPECT_FALSE(plan_attach(windows).has_value());
}

TEST(DesktopLayout, IconsInProgmanWithoutAWallpaperLayerIsNotReady) {
    // What the desktop looks like before Explorer has been asked to create the layer.
    const std::vector<WindowInfo> windows{
        {0x60, 0, L"Progman"},
        {0x61, 0x60, L"SHELLDLL_DefView"},
    };
    EXPECT_FALSE(plan_attach(windows).has_value());
}

TEST(DesktopLayout, NoDesktopAtAll) {
    // Explorer is not running (it crashed or is restarting).
    EXPECT_FALSE(plan_attach(std::vector<WindowInfo>{}).has_value());
    EXPECT_FALSE(plan_attach(std::vector<WindowInfo>{{0x10, 0, L"Notepad"}}).has_value());
}

TEST(DesktopLayout, ChildrenOfOtherWindowsAreIgnored) {
    // An icon view inside some other app's WorkerW child must not be mistaken for the desktop.
    const std::vector<WindowInfo> windows{
        {0x10, 0, L"SomeApp"},
        {0x11, 0x10, L"WorkerW"},
        {0x12, 0x11, L"SHELLDLL_DefView"},
        {0x60, 0, L"Progman"},
        {0x61, 0x60, L"SHELLDLL_DefView"},
        {0x62, 0x60, L"WorkerW"},
    };
    const auto plan = plan_attach(windows);
    ASSERT_TRUE(plan.has_value());
    EXPECT_EQ(plan->layout, DesktopLayout::Modern);
    EXPECT_EQ(plan->parent, 0x60u);
}
