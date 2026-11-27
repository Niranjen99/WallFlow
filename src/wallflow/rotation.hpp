#pragma once

#include <optional>

namespace wallflow {

// When to move on to the next video, and when to get the next one ready.
//
// Times are plain seconds from any steady clock (the app passes real time, tests pass made-up
// numbers), so there is no hidden clock and the rules can be tested exactly.
//
// Rules:
// - A video plays for at least `interval` seconds of *playing* time. Time spent paused (a game
//   covering the screen, the PC locked) does not count, so a long game does not use up a
//   video nobody saw.
// - It only switches at the end of a loop, the one moment the swap is seamless anyway. So a
//   video can play longer than the interval (until its current loop ends), never shorter.
// - The waiting player is loaded `kPreloadLead` seconds before the loop end, not all the
//   time, so most of the time only one decoder holds memory. Because paused time does not
//   count, at that moment it is already certain whether the end will restart this loop or
//   switch, so the right video is loaded.
class Rotation {
public:
    // Seconds before a loop end at which the waiting player starts loading. Loading a 4K file
    // and seeking to the start measured well under a second on the development PC.
    static constexpr double kPreloadLead = 2.0;

    // No interval: never switch (one video, or rotation off).
    explicit Rotation(std::optional<double> interval) : interval_(interval) {}

    // A new video started playing (or is loaded paused) at `now`.
    void start_video(double now, bool paused);

    // Pauses or resumes the playing-time count.
    void set_paused(bool paused, double now);

    // Playing time of the current video so far.
    [[nodiscard]] double played(double now) const;

    // Whether the loop that ends `remaining` seconds of playing time from `now` should be
    // followed by the next video (true) or by the same loop again (false).
    [[nodiscard]] bool switch_at_loop_end(double now, double remaining) const;

    // How long to wait (in playing time) before loading the waiting player, for a loop end
    // `remaining` seconds away. 0 = load now.
    [[nodiscard]] static double preload_delay(double remaining);

    // Loops this short (or shorter) keep the waiting player loaded all the time: there would
    // be no time to unload and load it again between two loop ends.
    [[nodiscard]] static bool keep_waiting_loaded(double loop_length);

private:
    std::optional<double> interval_;
    double played_before_ = 0;            // playing time up to the last pause
    std::optional<double> playing_since_; // set while playing
};

}  // namespace wallflow
