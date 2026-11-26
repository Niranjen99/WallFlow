// MpvPlayer with no video output (window == nullptr), so these run on CI without a screen.
// The video comes from FFmpeg's lavfi "testsrc" generator, so no media file is needed.

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <mutex>

#include <gtest/gtest.h>

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

}  // namespace

TEST(MpvPlayer, PlaysAGeneratedTestVideo) {
    Pump pump;
    MpvPlayer player(nullptr, pump.wakeup());
    player.play_url(kTestPattern);

    ASSERT_TRUE(pump.run_until(player, [&] { return player.started(); }));
    EXPECT_EQ(player.files_loaded(), 1);
    EXPECT_FALSE(player.last_error().has_value());
}

TEST(MpvPlayer, LoopsInsteadOfStopping) {
    // The test pattern is 1 second long. With loop-file=inf, after 1.5 seconds it must still
    // be playing (position is known) and must have wrapped back below 1 second.
    Pump pump;
    MpvPlayer player(nullptr, pump.wakeup());
    player.play_url(kTestPattern);
    ASSERT_TRUE(pump.run_until(player, [&] { return player.files_loaded() == 1; }));

    bool wrapped = false;
    double highest = 0;
    pump.run_until(
        player,
        [&] {
            if (const auto pos = player.position()) {
                if (*pos + 0.3 < highest) {
                    wrapped = true;  // position jumped back: the loop restarted
                }
                highest = std::max(highest, *pos);
            }
            return wrapped;
        },
        5s);
    EXPECT_TRUE(wrapped) << "highest position seen: " << highest;
}

TEST(MpvPlayer, MissingFileReportsAnError) {
    Pump pump;
    MpvPlayer player(nullptr, pump.wakeup());
    player.play(std::filesystem::temp_directory_path() / L"wallflow-no-such-file-é.mp4");

    ASSERT_TRUE(pump.run_until(player, [&] { return player.last_error().has_value(); }));
    EXPECT_EQ(player.files_loaded(), 0);
    EXPECT_FALSE(player.started());  // the window would stay hidden: normal wallpaper remains
}

TEST(MpvPlayer, PauseStopsThePosition) {
    Pump pump;
    MpvPlayer player(nullptr, pump.wakeup());
    player.play_url("av://lavfi:testsrc=size=320x240:rate=30:duration=10");
    ASSERT_TRUE(pump.run_until(player, [&] { return player.started(); }));

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
