#include <string>

#include <gtest/gtest.h>

#include "wallflow/settings.hpp"

using wallflow::format_time;
using wallflow::parse_settings;
using wallflow::parse_time;
using wallflow::Settings;
using wallflow::settings_to_json;

TEST(Settings, EmptyObjectGivesDefaults) {
    const auto parsed = parse_settings("{}");
    EXPECT_TRUE(parsed.valid_json);
    EXPECT_TRUE(parsed.warnings.empty());
    EXPECT_EQ(parsed.settings, Settings{});
    EXPECT_TRUE(parsed.settings.mute);  // audio is off unless the user turns it on
}

TEST(Settings, ReadsEveryField) {
    // Raw string: the backslashes are JSON escapes, as a user would type them.
    const auto parsed = parse_settings(R"({
        // comments are allowed
        "folder": "C:\\Users\\me\\Vidéos",
        "interval": "2:30",
        "shuffle": true,
        "mute": false,
        "volume": 35,
        "lock_screen_picture": true,
        "start_with_windows": true,
        "videos": {
            "goku-hero.mp4": { "start": "0:02", "end": 5.5 },
            "only-start.mkv": { "start": 3 }
        }
    })");
    ASSERT_TRUE(parsed.valid_json);
    EXPECT_TRUE(parsed.warnings.empty()) << parsed.warnings.front();
    const Settings& s = parsed.settings;
    EXPECT_EQ(s.folder, std::filesystem::path(L"C:\\Users\\me\\Vidéos"));
    EXPECT_EQ(s.interval, 150.0);
    EXPECT_TRUE(s.shuffle);
    EXPECT_FALSE(s.mute);
    EXPECT_EQ(s.volume, 35);
    EXPECT_TRUE(s.lock_screen_picture);
    EXPECT_TRUE(s.start_with_windows);
    ASSERT_EQ(s.loops.size(), 2u);
    EXPECT_EQ(s.loop_for(L"D:/anywhere/GOKU-HERO.MP4"), (wallflow::LoopSetting{2.0, 5.5}));
    EXPECT_EQ(s.loop_for(L"only-start.mkv"), (wallflow::LoopSetting{3.0, std::nullopt}));
    EXPECT_EQ(s.loop_for(L"other.mp4"), wallflow::LoopSetting{});
}

TEST(Settings, BadValuesKeepDefaultsWithAWarningEach) {
    const auto parsed = parse_settings(R"({
        "folder": 5,
        "interval": "5",
        "shuffle": "yes",
        "volume": 150,
        "videos": { "a.mp4": { "start": "soon" }, "b.mp4": 7 },
        "suffle": true
    })");
    ASSERT_TRUE(parsed.valid_json);
    EXPECT_EQ(parsed.settings, Settings{});
    EXPECT_EQ(parsed.warnings.size(), 7u);  // folder, interval, shuffle, volume, a, b, "suffle"
}

TEST(Settings, NotJsonIsReportedSoTheAppCanKeepWhatItHas) {
    for (const char* text : {"", "{ \"mute\": ", "[1, 2]", "mute = false"}) {
        const auto parsed = parse_settings(text);
        EXPECT_FALSE(parsed.valid_json) << text;
        EXPECT_EQ(parsed.settings, Settings{});
    }
}

TEST(Settings, SaveAndLoadGiveTheSameSettings) {
    Settings s;
    s.folder = L"C:\\Users\\me\\Vidéos\\Wallpapers";
    s.interval = 90.5;
    s.shuffle = true;
    s.mute = false;
    s.volume = 80;
    s.start_with_windows = true;
    s.loops[L"clip é.mp4"] = {1.25, 4.0};
    s.loops[L"end-only.mp4"] = {std::nullopt, 10.0};

    const std::string json = settings_to_json(s);
    const auto parsed = parse_settings(json);
    EXPECT_TRUE(parsed.warnings.empty());
    EXPECT_EQ(parsed.settings, s) << json;
}

TEST(Settings, SavedTimesAreReadable) {
    Settings s;
    s.interval = 300;
    s.loops[L"a.mp4"] = {2.0, 65.5};
    s.compatible_decoding.insert(L"a.mp4");
    s.compatible_decoding.insert(L"only-compatible.mp4");
    const std::string json = settings_to_json(s);
    EXPECT_NE(json.find(R"("interval": "5:00")"), std::string::npos) << json;
    EXPECT_NE(json.find(R"("start": "0:02")"), std::string::npos) << json;
    EXPECT_NE(json.find(R"("end": "1:05.5")"), std::string::npos) << json;
}

TEST(FormatTime, Examples) {
    EXPECT_EQ(format_time(0), "0:00");
    EXPECT_EQ(format_time(5), "0:05");
    EXPECT_EQ(format_time(90), "1:30");
    EXPECT_EQ(format_time(2.5), "0:02.5");
    EXPECT_EQ(format_time(3723.25), "1:02:03.25");
    EXPECT_EQ(format_time(0.1 + 0.2), "0:00.3");  // no 0.30000000000000004
    EXPECT_EQ(format_time(-4), "0:00");
}

TEST(FormatTime, ReadsBackTheSame) {
    for (const double t : {0.0, 1.0, 59.999, 60.0, 61.5, 3599.0, 3600.0, 7322.125}) {
        const std::string text = format_time(t);
        const auto back = parse_time(std::wstring(text.begin(), text.end()));
        ASSERT_TRUE(back.has_value()) << text;
        EXPECT_NEAR(*back, t, 1e-9) << text;
    }
}

TEST(Settings, CompatibleDecodingPerVideo) {
    const auto parsed = wallflow::parse_settings(R"({
        "videos": {
            "Glitchy.MP4": { "compatible_decoding": true },
            "fine.mp4": { "compatible_decoding": false, "start": 2 },
            "bad.mp4": { "compatible_decoding": "yes" }
        }
    })");
    const wallflow::Settings& s = parsed.settings;
    EXPECT_TRUE(s.compatible_for(L"C:/v/glitchy.mp4"));  // ignoring case
    EXPECT_FALSE(s.compatible_for(L"fine.mp4"));
    EXPECT_FALSE(s.compatible_for(L"bad.mp4"));
    EXPECT_EQ(parsed.warnings.size(), 1u);  // "yes" is not true/false
    // Written and read back: the same settings.
    EXPECT_EQ(wallflow::parse_settings(wallflow::settings_to_json(s)).settings, s);
}
