#include <limits>
#include <string>

#include <gtest/gtest.h>

#include "wallflow/loop_range.hpp"

using wallflow::LoopRange;
using wallflow::LoopSetting;
using wallflow::parse_time;
using wallflow::resolve_loop;

namespace {
const LoopRange kWhole{0, std::nullopt};
}  // namespace

TEST(LoopRange, NoSettingLoopsTheWholeFile) {
    EXPECT_EQ(resolve_loop({}, 30.0), kWhole);
    EXPECT_EQ(resolve_loop({}, std::nullopt), kWhole);
}

TEST(LoopRange, ValidRangeIsKept) {
    EXPECT_EQ(resolve_loop({5.0, 20.0}, 30.0), (LoopRange{5, 20.0}));
}

TEST(LoopRange, StartOnlyPlaysToTheEnd) {
    EXPECT_EQ(resolve_loop({5.0, std::nullopt}, 30.0), (LoopRange{5, std::nullopt}));
}

TEST(LoopRange, EndOnlyStartsAtZero) {
    EXPECT_EQ(resolve_loop({std::nullopt, 12.5}, 30.0), (LoopRange{0, 12.5}));
}

TEST(LoopRange, EndPastTheFileMeansToTheEnd) {
    EXPECT_EQ(resolve_loop({5.0, 99.0}, 30.0), (LoopRange{5, std::nullopt}));
    EXPECT_EQ(resolve_loop({5.0, 30.0}, 30.0), (LoopRange{5, std::nullopt}));
}

TEST(LoopRange, StartPastTheFileFallsBackToWhole) {
    EXPECT_EQ(resolve_loop({45.0, 50.0}, 30.0), kWhole);
    EXPECT_EQ(resolve_loop({30.0, std::nullopt}, 30.0), kWhole);
}

TEST(LoopRange, EndBeforeStartFallsBackToWhole) {
    EXPECT_EQ(resolve_loop({20.0, 5.0}, 30.0), kWhole);
}

TEST(LoopRange, TooShortFallsBackToWhole) {
    EXPECT_EQ(resolve_loop({5.0, 5.2}, 30.0), kWhole);
    EXPECT_EQ(resolve_loop({29.8, std::nullopt}, 30.0), kWhole);  // 0.2 s before the end
    EXPECT_EQ(resolve_loop({5.0, 5.5}, 30.0), (LoopRange{5, 5.5}));  // exactly the minimum
}

TEST(LoopRange, BadNumbersAreIgnored) {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    EXPECT_EQ(resolve_loop({-3.0, 10.0}, 30.0), (LoopRange{0, 10.0}));
    EXPECT_EQ(resolve_loop({nan, 10.0}, 30.0), (LoopRange{0, 10.0}));
    EXPECT_EQ(resolve_loop({5.0, inf}, 30.0), (LoopRange{5, std::nullopt}));
}

TEST(LoopRange, UnknownDurationTrustsTheSetting) {
    EXPECT_EQ(resolve_loop({5.0, 20.0}, std::nullopt), (LoopRange{5, 20.0}));
    EXPECT_EQ(resolve_loop({20.0, 5.0}, std::nullopt), kWhole);
}

TEST(ParseTime, Seconds) {
    EXPECT_EQ(parse_time(L"90"), 90.0);
    EXPECT_EQ(parse_time(L"0"), 0.0);
    EXPECT_DOUBLE_EQ(*parse_time(L"12.25"), 12.25);
    EXPECT_DOUBLE_EQ(*parse_time(L".5"), 0.5);
}

TEST(ParseTime, MinutesAndHours) {
    EXPECT_EQ(parse_time(L"1:30"), 90.0);
    EXPECT_DOUBLE_EQ(*parse_time(L"1:02:03.5"), 3723.5);
    EXPECT_EQ(parse_time(L"75:00"), 4500.0);  // the first part may be any size
}

TEST(ParseTime, RejectsTypos) {
    for (const wchar_t* bad : {L"", L":30", L"1:", L"1::2", L"1:75", L"1.5:30", L"1:2:3:4",
                               L"abc", L"5s", L"-5", L" 5", L"."}) {
        EXPECT_FALSE(parse_time(bad).has_value())
            << "accepted: " << ::testing::PrintToString(std::wstring(bad));
    }
}
