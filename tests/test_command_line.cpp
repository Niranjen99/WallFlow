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

TEST(CommandLine, Errors) {
    EXPECT_FALSE(parse({L"clip.mp4", L"--start"}).error.empty());           // no value
    EXPECT_FALSE(parse({L"clip.mp4", L"--start", L"soon"}).error.empty());  // not a time
    EXPECT_FALSE(parse({L"clip.mp4", L"--speed", L"2"}).error.empty());     // unknown option
    EXPECT_FALSE(parse({L"a.mp4", L"b.mp4"}).error.empty());                // two videos
}
