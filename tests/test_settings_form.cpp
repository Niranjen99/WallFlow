#include "wallflow/settings_form.hpp"

#include <gtest/gtest.h>

using wallflow::LoopSetting;
using wallflow::read_interval;
using wallflow::read_loop;
using wallflow::Settings;

TEST(SettingsForm, TimeTextMatchesTheSettingsFile) {
    EXPECT_EQ(wallflow::time_text(90.0), L"1:30");
    EXPECT_EQ(wallflow::time_text(2.5), L"0:02.5");
    EXPECT_EQ(wallflow::time_text(std::optional<double>{}), L"");
}

TEST(SettingsForm, SecondsTextIsAPlainNumber) {
    EXPECT_EQ(wallflow::seconds_text(300), L"300");
    EXPECT_EQ(wallflow::seconds_text(2.5), L"2.5");
    EXPECT_EQ(wallflow::seconds_text(10.005), L"10.005");
    EXPECT_EQ(wallflow::seconds_text(0), L"0");
}

TEST(SettingsForm, IntervalAcceptsTimesAndTrimsSpaces) {
    EXPECT_EQ(read_interval(L"5:00").seconds, 300);
    EXPECT_EQ(read_interval(L"  90 ").seconds, 90);
    EXPECT_EQ(read_interval(L"1:00:00").seconds, 3600);
}

TEST(SettingsForm, IntervalExplainsWhatIsWrong) {
    EXPECT_FALSE(read_interval(L"").seconds);
    EXPECT_FALSE(read_interval(L"").error.empty());
    EXPECT_FALSE(read_interval(L"five").seconds);
    EXPECT_NE(read_interval(L"five").error.find(L"Not a number"), std::wstring::npos);
    EXPECT_FALSE(read_interval(L"0:05").seconds);  // under the 10 s minimum
    EXPECT_NE(read_interval(L"0:05").error.find(L"10 seconds"), std::wstring::npos);
}

TEST(SettingsForm, LoopFieldsMayBeEmpty) {
    EXPECT_EQ(read_loop(L"", L"").loop, LoopSetting{});
    EXPECT_EQ(read_loop(L"0:02", L"").loop, (LoopSetting{2.0, std::nullopt}));
    EXPECT_EQ(read_loop(L" ", L"5").loop, (LoopSetting{std::nullopt, 5.0}));
    EXPECT_EQ(read_loop(L"0:02", L"0:05.5").loop, (LoopSetting{2.0, 5.5}));
}

TEST(SettingsForm, LoopFieldsRejectBadTimesAndTooShortLoops) {
    EXPECT_NE(read_loop(L"abc", L"").error.find(L"start"), std::wstring::npos);
    EXPECT_NE(read_loop(L"", L"1:7x").error.find(L"end"), std::wstring::npos);
    EXPECT_FALSE(read_loop(L"0:05", L"0:02").loop);    // end before start
    EXPECT_FALSE(read_loop(L"3", L"3.4").loop);        // shorter than half a second
    EXPECT_TRUE(read_loop(L"3", L"3.5").loop);
}

TEST(SettingsForm, SetLoopReplacesAnEntryThatDiffersOnlyInCase) {
    Settings settings;
    settings.loops[L"Clip.MP4"] = {1.0, 2.0};
    wallflow::set_loop(settings, L"clip.mp4", {3.0, 6.0});
    ASSERT_EQ(settings.loops.size(), 1u);
    EXPECT_EQ(settings.loops.at(L"clip.mp4"), (LoopSetting{3.0, 6.0}));
}

TEST(SettingsForm, EmptyLoopRemovesTheEntry) {
    Settings settings;
    settings.loops[L"clip.mp4"] = {1.0, 2.0};
    settings.loops[L"other.mp4"] = {1.0, 2.0};
    wallflow::set_loop(settings, L"CLIP.mp4", {});
    EXPECT_EQ(settings.loops.count(L"clip.mp4"), 0u);
    EXPECT_EQ(settings.loops.size(), 1u);
}

TEST(SettingsForm, VideoRowsShowEachVideosLoop) {
    Settings settings;
    settings.loops[L"b.mp4"] = {2.0, std::nullopt};
    const auto rows = wallflow::video_rows({L"C:\\v\\a.mp4", L"C:\\v\\B.MP4"}, settings);
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].name, L"a.mp4");
    EXPECT_EQ(rows[0].start, L"");
    EXPECT_EQ(rows[1].name, L"B.MP4");
    EXPECT_EQ(rows[1].start, L"0:02");  // matched ignoring case
    EXPECT_EQ(rows[1].end, L"");
}

TEST(SettingsForm, SetCompatibleReplacesAnEntryThatDiffersOnlyInCase) {
    Settings settings;
    settings.compatible_decoding.insert(L"Clip.MP4");
    wallflow::set_compatible(settings, L"clip.mp4", true);
    EXPECT_EQ(settings.compatible_decoding.size(), 1u);
    EXPECT_TRUE(settings.compatible_for(L"CLIP.mp4"));
    wallflow::set_compatible(settings, L"CLIP.MP4", false);
    EXPECT_TRUE(settings.compatible_decoding.empty());
    const auto rows = wallflow::video_rows({L"clip.mp4"}, settings);
    EXPECT_FALSE(rows[0].compatible);
}

TEST(SettingsForm, OnlyVideoRowIgnoresCase) {
    Settings settings;
    settings.only_video = L"CLIP.mp4";
    settings.compatible_decoding.insert(L"clip.mp4");
    const auto rows = wallflow::video_rows({L"C:/v/clip.MP4", L"C:/v/other.mp4"}, settings);
    EXPECT_TRUE(rows[0].only);
    EXPECT_FALSE(rows[1].only);
    EXPECT_EQ(wallflow::options_text(rows[0]), L"Only, compatible");
    EXPECT_EQ(wallflow::options_text(rows[1]), L"");
    EXPECT_EQ(wallflow::options_text({L"a", L"", L"", true, false}), L"Compatible");
    EXPECT_EQ(wallflow::options_text({L"a", L"", L"", false, true}), L"Only this");
}
