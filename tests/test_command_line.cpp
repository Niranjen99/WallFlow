#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "wallflow/command_line.hpp"

using wallflow::parse_command_line;

namespace {
wallflow::CommandLine parse(std::vector<std::wstring> args) {
    return parse_command_line(args);
}
}  // namespace

TEST(CommandLine, NothingGiven) {
    const auto cl = parse({});
    EXPECT_TRUE(cl.video.empty());
    EXPECT_FALSE(cl.loop.start || cl.loop.end);
    EXPECT_TRUE(cl.error.empty());
}

TEST(CommandLine, VideoOnly) {
    const auto cl = parse({L"C:/Videos/my clip é.mp4"});
    EXPECT_EQ(cl.video, L"C:/Videos/my clip é.mp4");
    EXPECT_TRUE(cl.error.empty());
}

TEST(CommandLine, VideoWithLoopRangeInAnyOrder) {
    const auto cl = parse({L"--end", L"0:20", L"clip.mp4", L"--start", L"5.5"});
    EXPECT_EQ(cl.video, L"clip.mp4");
    EXPECT_EQ(cl.loop.start, 5.5);
    EXPECT_EQ(cl.loop.end, 20.0);
    EXPECT_TRUE(cl.error.empty());
}

TEST(CommandLine, Folder) {
    const auto cl = parse({L"--folder", L"C:/Wallpapers", L"--interval", L"2:30", L"--shuffle"});
    EXPECT_EQ(cl.folder, L"C:/Wallpapers");
    EXPECT_EQ(cl.interval, 150.0);
    EXPECT_TRUE(cl.shuffle);
    EXPECT_TRUE(cl.video.empty());
    EXPECT_TRUE(cl.error.empty());
}

TEST(CommandLine, FolderDefaults) {
    const auto cl = parse({L"--folder", L"C:/Wallpapers"});
    EXPECT_EQ(cl.interval, 300.0);  // five minutes
    EXPECT_FALSE(cl.shuffle);
    EXPECT_TRUE(cl.error.empty());
}

TEST(CommandLine, FolderErrors) {
    EXPECT_FALSE(parse({L"--folder"}).error.empty());                                    // no value
    EXPECT_FALSE(parse({L"clip.mp4", L"--folder", L"C:/W"}).error.empty());              // both
    EXPECT_FALSE(parse({L"--folder", L"C:/W", L"--start", L"5"}).error.empty());         // loop
    EXPECT_FALSE(parse({L"--folder", L"C:/W", L"--interval", L"5"}).error.empty());      // 5 s
    EXPECT_FALSE(parse({L"clip.mp4", L"--shuffle"}).error.empty());                      // no folder
    EXPECT_FALSE(parse({L"clip.mp4", L"--interval", L"5:00"}).error.empty());            // no folder
}

TEST(CommandLine, Errors) {
    EXPECT_FALSE(parse({L"clip.mp4", L"--start"}).error.empty());           // no value
    EXPECT_FALSE(parse({L"clip.mp4", L"--start", L"soon"}).error.empty());  // not a time
    EXPECT_FALSE(parse({L"clip.mp4", L"--speed", L"2"}).error.empty());     // unknown option
    EXPECT_FALSE(parse({L"a.mp4", L"b.mp4"}).error.empty());                // two videos
}
