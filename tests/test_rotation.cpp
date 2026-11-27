#include <gtest/gtest.h>

#include "wallflow/rotation.hpp"

using wallflow::Rotation;

// Times in these tests are seconds on a made-up clock.

TEST(Rotation, NoIntervalNeverSwitches) {
    Rotation rotation(std::nullopt);
    rotation.start_video(0, false);
    EXPECT_FALSE(rotation.switch_at_loop_end(1e6, 10));
}

TEST(Rotation, SwitchesAtTheFirstLoopEndAfterTheInterval) {
    // 5-minute interval, 40-second loop: loop ends at 40, 80, ... 280, 320.
    Rotation rotation(300.0);
    rotation.start_video(1000, false);
    EXPECT_FALSE(rotation.switch_at_loop_end(1000 + 238, 2));  // loop ending at 240
    EXPECT_FALSE(rotation.switch_at_loop_end(1000 + 278, 2));  // ending at 280: still < 300
    EXPECT_TRUE(rotation.switch_at_loop_end(1000 + 318, 2));   // ending at 320: >= 300
}

TEST(Rotation, ExactlyAtTheIntervalSwitches) {
    Rotation rotation(60.0);
    rotation.start_video(0, false);
    EXPECT_TRUE(rotation.switch_at_loop_end(58, 2));
}

TEST(Rotation, AVideoLongerThanTheIntervalPlaysToItsEnd) {
    // Never cut a loop short: a 10-minute video with a 5-minute interval switches at 10 min.
    Rotation rotation(300.0);
    rotation.start_video(0, false);
    EXPECT_TRUE(rotation.switch_at_loop_end(598, 2));
}

TEST(Rotation, PausedTimeDoesNotCount) {
    Rotation rotation(60.0);
    rotation.start_video(0, false);
    rotation.set_paused(true, 30);   // a game starts after 30 s
    rotation.set_paused(false, 3630);  // ends an hour later
    EXPECT_DOUBLE_EQ(rotation.played(3640), 40);
    EXPECT_FALSE(rotation.switch_at_loop_end(3640, 10));  // 50 s played at the loop end
    EXPECT_TRUE(rotation.switch_at_loop_end(3640, 20));   // 60 s
}

TEST(Rotation, PauseAndResumeTwiceAreHarmless) {
    Rotation rotation(60.0);
    rotation.start_video(0, false);
    rotation.set_paused(true, 10);
    rotation.set_paused(true, 20);   // already paused: the second pause changes nothing
    rotation.set_paused(false, 30);
    rotation.set_paused(false, 40);  // already playing: does not restart the count
    EXPECT_DOUBLE_EQ(rotation.played(50), 30);
}

TEST(Rotation, AVideoLoadedPausedStartsCountingOnResume) {
    Rotation rotation(60.0);
    rotation.start_video(0, true);
    EXPECT_DOUBLE_EQ(rotation.played(100), 0);
    rotation.set_paused(false, 100);
    EXPECT_DOUBLE_EQ(rotation.played(110), 10);
}

TEST(Rotation, NewVideoStartsFromZero) {
    Rotation rotation(60.0);
    rotation.start_video(0, false);
    rotation.start_video(70, false);
    EXPECT_DOUBLE_EQ(rotation.played(80), 10);
}

TEST(Rotation, ChangingTheIntervalKeepsThePlayedTime) {
    Rotation rotation(300.0);
    rotation.start_video(0, false);
    rotation.set_interval(60.0);  // the user shortened it after 50 s
    EXPECT_TRUE(rotation.switch_at_loop_end(50, 10));
    rotation.set_interval(std::nullopt);  // rotation turned off
    EXPECT_FALSE(rotation.switch_at_loop_end(50, 10));
}

TEST(Rotation, PreloadsShortlyBeforeTheLoopEnd) {
    EXPECT_DOUBLE_EQ(Rotation::preload_delay(30), 30 - Rotation::kPreloadLead);
    EXPECT_DOUBLE_EQ(Rotation::preload_delay(1), 0);  // already too close: load now
}

TEST(Rotation, ShortLoopsKeepTheWaitingPlayerLoaded) {
    EXPECT_TRUE(Rotation::keep_waiting_loaded(3));
    EXPECT_TRUE(Rotation::keep_waiting_loaded(2 * Rotation::kPreloadLead));
    EXPECT_FALSE(Rotation::keep_waiting_loaded(10));
}
