#include <gtest/gtest.h>

#include "wallflow/win/autostart.hpp"

using namespace wallflow::win;

// Uses its own value name, so the real "WallFlow" entry (if any) is never touched.
TEST(Autostart, AddsReadsAndRemovesTheRunEntry) {
    const std::wstring name = L"WallFlow.UnitTest";
    set_autostart(name, L"", false);  // clean start
    EXPECT_FALSE(autostart_command(name).has_value());

    const std::wstring command = own_command_line();
    ASSERT_TRUE(set_autostart(name, command, true));
    EXPECT_EQ(autostart_command(name), command);

    ASSERT_TRUE(set_autostart(name, L"", false));
    EXPECT_FALSE(autostart_command(name).has_value());
    EXPECT_TRUE(set_autostart(name, L"", false));  // removing twice is fine
}

TEST(Autostart, OwnCommandLineIsTheQuotedExePath) {
    const std::wstring command = own_command_line();
    ASSERT_GE(command.size(), 6u);
    EXPECT_EQ(command.front(), L'"');
    EXPECT_EQ(command.back(), L'"');
    EXPECT_NE(command.find(L"wallflow_tests.exe"), std::wstring::npos);
}
