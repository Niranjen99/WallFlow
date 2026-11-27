// MpvPlayer with no video output (window == nullptr), so these run on CI without a screen.
// The videos come from FFmpeg's lavfi "testsrc" generator, so no media file is needed. The
// generator cannot seek and has no known length, so tests that need either use test_video().

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>

#include <gtest/gtest.h>
#include <mpv/client.h>

#include "wallflow/win/mpv_player.hpp"

using namespace std::chrono_literals;
using wallflow::win::MpvPlayer;

namespace {

// Stands in for the app's message loop: mpv's wakeup (on an mpv thread) sets a flag and wakes
// the test thread, which then processes events, the way PostMessage + WM_APP do in the app.
class Pump {
public:
    std::function<void()> wakeup() {
        return [this] {
            {
                std::lock_guard lock(mutex_);
                pending_ = true;
            }
            cv_.notify_one();
        };
    }

    // Processes events until `done()` is true or `timeout` passes. Returns done().
    // Wakes on every mpv wakeup, and at least every 50 ms, because some conditions (such as
    // the playback position) change without any event being sent.
    bool run_until(MpvPlayer& player, const std::function<bool()>& done,
                   std::chrono::milliseconds timeout = 10s) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (!done()) {
            if (std::chrono::steady_clock::now() > deadline) {
                return false;
            }
            {
                std::unique_lock lock(mutex_);
                cv_.wait_for(lock, 50ms, [this] { return pending_; });
                pending_ = false;
            }
            player.process_events();
        }
        return true;
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    bool pending_ = false;
};

constexpr char kTestPattern[] = "av://lavfi:testsrc=size=320x240:rate=30:duration=1";
constexpr char kLongPattern[] = "av://lavfi:testsrc=size=320x240:rate=30:duration=10";
constexpr double kOneFrame = 1.0 / 30;

// A real 4-second video file, made once per test run with mpv's own encoder (FFmpeg's mpeg4
// codec, built into libmpv), so CI needs no media files. Unlike the lavfi generator it has a
// known length, and keyframes only every 15 frames (0.5 s), so a start between keyframes
// really tests the exact seek.
const std::filesystem::path& test_video() {
    static const std::filesystem::path path = [] {
        auto file = std::filesystem::temp_directory_path() / "wallflow-test-4s.mkv";
        mpv_handle* mpv = mpv_create();
        mpv_set_option_string(mpv, "config", "no");
        mpv_set_option_string(mpv, "o", file.string().c_str());
        mpv_set_option_string(mpv, "ovc", "mpeg4");
        mpv_set_option_string(mpv, "ovcopts", "g=15");
        mpv_initialize(mpv);
        const char* command[] = {
            "loadfile", "av://lavfi:testsrc=size=160x120:rate=30:duration=4", nullptr};
        mpv_command(mpv, command);
        for (;;) {  // the encode takes well under a second
            const mpv_event* event = mpv_wait_event(mpv, 10);
            if (event->event_id == MPV_EVENT_END_FILE || event->event_id == MPV_EVENT_SHUTDOWN) {
                break;
            }
        }
        mpv_terminate_destroy(mpv);  // finishes writing the file
        return file;
    }();
    return path;
}

}  // namespace

TEST(MpvPlayer, PlaysAGeneratedTestVideo) {
    Pump pump;
    MpvPlayer player(nullptr, pump.wakeup());
    player.load_url(kTestPattern, {}, false);

    ASSERT_TRUE(pump.run_until(player, [&] { return player.ready(); }));
    EXPECT_EQ(player.files_loaded(), 1);
    EXPECT_FALSE(player.last_error().has_value());
}

TEST(MpvPlayer, HoldsTheLastFrameAtTheEnd) {
    // The test pattern is 1 second long. At the end the file stays loaded and paused.
    Pump pump;
    MpvPlayer player(nullptr, pump.wakeup());
    player.load_url(kTestPattern, {}, false);

    ASSERT_TRUE(pump.run_until(player, [&] { return player.at_end(); }, 5s));
    EXPECT_TRUE(player.paused());
    ASSERT_TRUE(player.position().has_value()) << "the file was unloaded";
    EXPECT_GT(*player.position(), 0.9);
}

TEST(MpvPlayer, PausedLoadShowsTheLoopStart) {
    Pump pump;
    MpvPlayer player(nullptr, pump.wakeup());
    player.load(test_video(), {1.2, std::nullopt}, true);  // keyframes at 1.0 and 1.5

    ASSERT_TRUE(pump.run_until(player, [&] { return player.ready(); }));
    EXPECT_TRUE(player.paused());
    ASSERT_TRUE(player.position().has_value());
    EXPECT_NEAR(*player.position(), 1.2, kOneFrame);  // exact seek, not the keyframe before
}

TEST(MpvPlayer, StopsAtTheLoopEnd) {
    Pump pump;
    MpvPlayer player(nullptr, pump.wakeup());
    player.load(test_video(), {1.2, 1.8}, false);

    ASSERT_TRUE(pump.run_until(player, [&] { return player.at_end(); }, 5s));
    ASSERT_TRUE(player.position().has_value());
    EXPECT_NEAR(*player.position(), 1.8, 2 * kOneFrame);
}

TEST(MpvPlayer, RewindGoesBackToTheStartAndStaysPaused) {
    Pump pump;
    MpvPlayer player(nullptr, pump.wakeup());
    player.load(test_video(), {1.2, 1.8}, false);
    ASSERT_TRUE(pump.run_until(player, [&] { return player.at_end(); }, 5s));

    player.rewind();
    EXPECT_FALSE(player.at_end());
    ASSERT_TRUE(pump.run_until(player, [&] { return player.ready(); }));
    EXPECT_TRUE(player.paused());
    EXPECT_NEAR(*player.position(), 1.2, kOneFrame);

    player.set_paused(false);  // and the loop plays again, to the end
    EXPECT_TRUE(pump.run_until(player, [&] { return player.at_end(); }, 5s));
}

TEST(MpvPlayer, StartPastTheEndFallsBackToTheWholeFile) {
    // 4-second video, loop asked for 5..9 s: only known to be wrong once the file is loaded.
    Pump pump;
    MpvPlayer player(nullptr, pump.wakeup());
    player.load(test_video(), {5.0, 9.0}, true);
    ASSERT_TRUE(pump.run_until(player, [&] { return player.files_loaded() == 1; }));

    EXPECT_EQ(player.range(), (wallflow::LoopRange{0, std::nullopt}));
    ASSERT_TRUE(pump.run_until(player, [&] { return player.ready(); }));
    EXPECT_NEAR(*player.position(), 0.0, kOneFrame);
}

TEST(MpvPlayer, LoopEndIsCheckedAgainstTheRealLength) {
    // 3.8..10 s on a 4-second video: the start is valid, but only 0.2 s are left (too short).
    Pump pump;
    MpvPlayer player(nullptr, pump.wakeup());
    player.load(test_video(), {3.8, 10.0}, false);
    ASSERT_TRUE(pump.run_until(player, [&] { return player.files_loaded() == 1; }));
    EXPECT_EQ(player.range(), (wallflow::LoopRange{0, std::nullopt}));

    // The end set at load time (10 s) was cleared: it plays to the real end, from 0.
    ASSERT_TRUE(pump.run_until(player, [&] { return player.at_end(); }, 10s));
    EXPECT_NEAR(*player.position(), 4.0, 2 * kOneFrame);
}

TEST(MpvPlayer, KnowsTheTimeLeftInTheLoop) {
    Pump pump;
    MpvPlayer player(nullptr, pump.wakeup());
    player.load(test_video(), {1.0, 3.0}, true);
    ASSERT_TRUE(pump.run_until(player, [&] { return player.ready(); }));
    ASSERT_TRUE(player.remaining().has_value());
    EXPECT_NEAR(*player.remaining(), 2.0, kOneFrame);
    EXPECT_NEAR(player.loop_length().value(), 2.0, 1e-9);

    player.load(test_video(), {1.0, std::nullopt}, true);  // no end: up to the file's end
    ASSERT_TRUE(pump.run_until(player, [&] { return player.ready(); }));
    EXPECT_NEAR(player.remaining().value(), 3.0, kOneFrame);
    EXPECT_NEAR(player.loop_length().value(), 3.0, kOneFrame);
}

TEST(MpvPlayer, UnloadsAndLoadsAgain) {
    Pump pump;
    MpvPlayer player(nullptr, pump.wakeup());
    player.load(test_video(), {}, true);
    ASSERT_TRUE(pump.run_until(player, [&] { return player.ready(); }));

    player.unload();
    EXPECT_FALSE(player.loaded());
    EXPECT_FALSE(player.ready());
    // Events of the old file still arrive after unload(); none may make it "ready" again.
    pump.run_until(player, [] { return false; }, 300ms);
    EXPECT_FALSE(player.ready());
    EXPECT_FALSE(player.at_end());
    EXPECT_FALSE(player.position().has_value());

    player.load(test_video(), {2.0, std::nullopt}, true);
    ASSERT_TRUE(pump.run_until(player, [&] { return player.ready(); }));
    EXPECT_NEAR(*player.position(), 2.0, kOneFrame);
}

TEST(MpvPlayer, MutedByDefaultAudioCanBeTurnedOn) {
    Pump pump;
    MpvPlayer player(nullptr, pump.wakeup());
    EXPECT_EQ(player.property("aid"), "no");  // not even decoded

    player.set_audio(false, 35);
    EXPECT_EQ(player.property("aid"), "auto");
    EXPECT_EQ(player.property("volume"), "35.000000");

    player.set_audio(true, 35);
    EXPECT_EQ(player.property("aid"), "no");
    player.set_audio(false, 250);  // out of range: clamped
    EXPECT_EQ(player.property("volume"), "100.000000");
}

TEST(MpvPlayer, CompatibleDecodingIsSetForThatFileOnly) {
    Pump pump;
    MpvPlayer player(nullptr, pump.wakeup());
    player.load(test_video(), {}, true, true);
    ASSERT_TRUE(pump.run_until(player, [&] { return player.ready(); }));
    EXPECT_EQ(player.property("hwdec"), "vulkan-copy");
    player.load(test_video(), {}, true);  // the next file: back to the default
    ASSERT_TRUE(pump.run_until(player, [&] { return player.ready(); }));
    EXPECT_EQ(player.property("hwdec"), "auto-safe");
}

TEST(MpvPlayer, MissingFileReportsAnError) {
    Pump pump;
    MpvPlayer player(nullptr, pump.wakeup());
    player.load(std::filesystem::temp_directory_path() / L"wallflow-no-such-file-é.mp4", {}, false);

    ASSERT_TRUE(pump.run_until(player, [&] { return player.last_error().has_value(); }));
    EXPECT_EQ(player.files_loaded(), 0);
    EXPECT_FALSE(player.ready());  // the window would stay hidden: normal wallpaper remains
}

TEST(MpvPlayer, PauseStopsThePosition) {
    Pump pump;
    MpvPlayer player(nullptr, pump.wakeup());
    player.load_url(kLongPattern, {}, false);
    ASSERT_TRUE(pump.run_until(player, [&] { return player.ready(); }));

    player.set_paused(true);
    ASSERT_TRUE(pump.run_until(player, [&] { return player.paused(); }));
    const auto before = player.position();
    ASSERT_TRUE(before.has_value());
    pump.run_until(player, [] { return false; }, 500ms);  // let half a second pass
    EXPECT_EQ(player.position(), before) << "the position moved while paused";

    player.set_paused(false);
    EXPECT_TRUE(pump.run_until(player, [&] {
        const auto now = player.position();
        return now && *now > *before + 0.2;
    })) << "the position did not move after resuming";
}
