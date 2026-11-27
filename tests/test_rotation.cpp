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

TEST(Rotation, SwitchesOnTimeInTheMiddleOfALoop) {
    Rotation rotation(30.0);
    rotation.start_video(0, false);
    // 20 s played, the 45 s loop ends in 25 s: the switch is due in 10 s, before the loop end.
    const auto due = rotation.switch_before_loop_end(20, 25, 45);
    ASSERT_TRUE(due.has_value());
    EXPECT_DOUBLE_EQ(*due, 10);
    // Already overdue (e.g. the interval was shortened): switch now.
    EXPECT_DOUBLE_EQ(*rotation.switch_before_loop_end(40, 5, 45), 0);
}

TEST(Rotation, SwitchesAtTheLoopEndWhenThatComesFirst) {
    Rotation rotation(30.0);
    rotation.start_video(0, false);
    EXPECT_FALSE(rotation.switch_before_loop_end(20, 10, 45).has_value());  // due at 30 = loop end
    EXPECT_FALSE(rotation.switch_before_loop_end(20, 5, 45).has_value());   // loop ends at 25
}

TEST(Rotation, PausedTimeDoesNotBringTheSwitchCloser) {
    Rotation rotation(30.0);
    rotation.start_video(0, false);
    rotation.set_paused(true, 10);  // 10 s played, then a game covers the screen for 100 s
    rotation.set_paused(false, 110);
    EXPECT_DOUBLE_EQ(*rotation.switch_before_loop_end(110, 40, 60), 20);
}

TEST(Rotation, WaitForLoopEndAndShortLoopsNeverSwitchMidLoop) {
    Rotation rotation(30.0);
    rotation.start_video(0, false);
    EXPECT_FALSE(rotation.switch_before_loop_end(20, 3, 4).has_value());  // short loop: at its end
    rotation.set_wait_for_loop_end(true);
    EXPECT_FALSE(rotation.switch_before_loop_end(20, 25, 45).has_value());
    Rotation off(std::nullopt);
    off.start_video(0, false);
    EXPECT_FALSE(off.switch_before_loop_end(1000, 25, 45).has_value());  // no rotation at all
}

TEST(Rotation, OnTimeSwitchJustAfterALoopEndMovesToThatLoopEnd) {
    Rotation rotation(10.0);
    rotation.start_video(0, false);
    // The loop ends at 9.98 s, the switch is due at 10 s: switch at the loop end, not 20 ms
    // after a loop restart.
    EXPECT_TRUE(rotation.switch_at_loop_end(8.0, 1.98));
    EXPECT_FALSE(rotation.switch_at_loop_end(8.0, 0.5));  // due 1.5 s later: loop first
    rotation.set_wait_for_loop_end(true);
    EXPECT_FALSE(rotation.switch_at_loop_end(8.0, 1.98));  // waiting mode: never early
}
