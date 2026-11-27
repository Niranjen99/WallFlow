#include "wallflow/win/video_engine.hpp"

#include <chrono>
#include <cmath>
#include <string>

#include "wallflow/win/log.hpp"

namespace wallflow::win {

namespace {

// After this many videos in a row fail to load, stop trying (e.g. a folder of broken files).
constexpr int kMaxFailures = 10;

// Seconds on a steady clock (never jumps when the PC's time is changed), for Rotation.
double now() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

std::wstring name_of(const VideoSource& video) {
    return video.file.filename().wstring();
}

}  // namespace

VideoEngine::VideoEngine(HINSTANCE instance, HWND controller, UINT player_message,
                         UINT_PTR preload_timer)
    : controller_(controller), player_message_(player_message), preload_timer_(preload_timer) {
    for (Slot& slot : slots_) {
        slot.window = std::make_unique<WallpaperWindow>(instance);
    }
}

VideoEngine::~VideoEngine() {
    ::KillTimer(controller_, preload_timer_);
}

std::optional<DesktopLayout> VideoEngine::attach() {
    std::optional<DesktopLayout> layout;
    for (Slot& slot : slots_) {
        layout = slot.window->attach();
        if (!layout) {
            return std::nullopt;
        }
    }
    return layout;
}

bool VideoEngine::alive() const noexcept {
    return slots_[0].window->alive() && slots_[1].window->alive();
}

void VideoEngine::play(const VideoSource& first, bool paused, std::optional<double> interval,
                       NextVideo next) {
    // The wakeup runs on an mpv thread: it only posts a message, and the UI thread does the
    // work in on_player_message(). Capturing the HWND by value is safe: PostMessageW to a
    // window that no longer exists just fails.
    HWND controller = controller_;
    const UINT message = player_message_;
    for (Slot& slot : slots_) {
        if (!slot.player) {
            slot.player = std::make_unique<MpvPlayer>(
                slot.window->hwnd(), [controller, message] {
                    ::PostMessageW(controller, message, 0, 0);
                });
        }
    }
    for (Slot& slot : slots_) {
        slot.player->set_audio(muted_, volume_);
    }
    paused_ = paused;
    playing_ = true;
    const bool wait = rotation_.wait_for_loop_end();
    rotation_ = Rotation(next ? interval : std::nullopt);
    rotation_.set_wait_for_loop_end(wait);
    next_ = std::move(next);
    rotation_.start_video(now(), paused);
    shown().video = first;
    shown().player->load(first.file, first.loop, paused, first.compatible);
    log(L"playing " + name_of(first));
    // The waiting player stays empty until shortly before the loop end (arm_preload).
}

void VideoEngine::set_paused(bool paused) {
    if (!playing_ || paused == paused_) {
        return;
    }
    paused_ = paused;
    shown().player->set_paused(paused);
    rotation_.set_paused(paused, now());
    if (paused) {
        // The timer counts wall-clock time, the loop only advances while playing: stop it and
        // start it again with the real time left on resume.
        ::KillTimer(controller_, preload_timer_);
        timer_armed_ = false;
    } else if (!waiting().video && shown().player->ready()) {
        arm_preload();
    } else if (mid_loop_switch_ && !switch_now_) {
        // The next video is loaded and waits for its moment: count down the time left again.
        const double remaining = shown().player->remaining().value_or(0);
        const double length = shown().player->loop_length().value_or(0);
        if (const auto due = rotation_.switch_before_loop_end(now(), remaining, length)) {
            arm_switch(*due);
        } else {
            mid_loop_switch_ = false;  // now it falls on the loop end: the normal swap does it
        }
    }
}

void VideoEngine::set_audio(bool muted, int volume) {
    muted_ = muted;
    volume_ = volume;
    for (Slot& slot : slots_) {
        if (slot.player) {
            slot.player->set_audio(muted, volume);
        }
    }
}

void VideoEngine::set_rotation(std::optional<double> interval, NextVideo next) {
    rotation_.set_interval(next ? interval : std::nullopt);
    next_ = std::move(next);
    replan();  // an on-time switch may now be earlier, later, or not happen at all
}

void VideoEngine::set_wait_for_loop_end(bool wait) {
    if (wait == rotation_.wait_for_loop_end()) {
        return;
    }
    rotation_.set_wait_for_loop_end(wait);
    replan();
}

void VideoEngine::replan() {
    if (!playing_ || gave_up_ || paused_ || switch_now_) {
        return;  // not playing, or resuming re-plans anyway
    }
    if (mid_loop_switch_ && waiting().video) {
        // A next video is loaded for an on-time switch that may no longer apply: drop it.
        ::KillTimer(controller_, preload_timer_);
        timer_armed_ = false;
        mid_loop_switch_ = false;
        timer_is_switch_ = false;
        waiting_is_switch_ = false;
        waiting().player->unload();
        waiting().video.reset();
    }
    if (!waiting().video && shown().player->ready()) {
        ::KillTimer(controller_, preload_timer_);
        timer_armed_ = false;
        arm_preload();
    }
}

void VideoEngine::arm_switch(double delay) {
    ::KillTimer(controller_, preload_timer_);
    timer_is_switch_ = true;
    ::SetTimer(controller_, preload_timer_, static_cast<UINT>(std::max(1L, std::lround(delay * 1000))),
               nullptr);
    timer_armed_ = true;
}

void VideoEngine::switch_now(const VideoSource& video) {
    if (!playing_ || gave_up_) {
        return;
    }
    ::KillTimer(controller_, preload_timer_);
    timer_armed_ = false;
    timer_is_switch_ = false;
    mid_loop_switch_ = false;
    Slot& wait = waiting();
    wait.player->load(video.file, video.loop, true, video.compatible);
    wait.video = video;
    waiting_is_switch_ = true;
    switch_now_ = true;
    log(L"switching now to " + name_of(video));
}

std::optional<VideoSource> VideoEngine::current() const {
    return slots_[shown_].video;
}

void VideoEngine::on_player_message() {
    if (!playing_) {
        return;
    }
    for (Slot& slot : slots_) {
        slot.player->process_events();
    }
    handle_errors();
    if (gave_up_) {
        return;
    }

    Slot& on = shown();
    if (!on.window->revealed() && on.player->ready()) {
        on.window->reveal();
        log(L"first frame ready: wallpaper revealed");
    }
    if (on.player->ready() && !waiting().video && !timer_armed_ && !paused_) {
        arm_preload();
    }
    if (switch_now_ && waiting().player->ready()) {
        swap();
        return;
    }
    if (on.player->at_end()) {
        if (!waiting().video) {
            prepare_waiting();  // the preload did not happen in time: hold the last frame
        } else if (waiting().player->ready()) {
            swap();
        }
    }
}

void VideoEngine::on_preload_timer() {
    ::KillTimer(controller_, preload_timer_);  // Windows timers repeat; this one is one-shot
    timer_armed_ = false;
    if (!playing_ || gave_up_) {
        return;
    }
    if (timer_is_switch_) {
        // Time is up: swap in the preloaded next video now (or as soon as it is ready).
        timer_is_switch_ = false;
        switch_now_ = true;
        if (waiting().player->ready()) {
            swap();
        }
        return;
    }
    if (!waiting().video) {
        prepare_waiting();
    }
}

void VideoEngine::arm_preload() {
    const std::optional<double> remaining = shown().player->remaining();
    const std::optional<double> length = shown().player->loop_length();
    // Unknown length (some streams), or a loop too short to unload in between: load now.
    if (!remaining || !length || Rotation::keep_waiting_loaded(*length)) {
        prepare_waiting();
        return;
    }
    // Whichever comes first: the loop end, or the time to switch (on-time switching).
    const auto due = rotation_.switch_before_loop_end(now(), *remaining, *length);
    const double delay = Rotation::preload_delay(due.value_or(*remaining));
    if (delay <= 0) {
        prepare_waiting();
        return;
    }
    timer_is_switch_ = false;
    ::SetTimer(controller_, preload_timer_, static_cast<UINT>(std::lround(delay * 1000)), nullptr);
    timer_armed_ = true;
}

void VideoEngine::prepare_waiting() {
    Slot& on = shown();
    Slot& wait = waiting();
    const double remaining = on.player->remaining().value_or(0);
    const double length = on.player->loop_length().value_or(0);

    // On time: the switch comes before the loop end, so load the next video now and swap it
    // in when the time is up (a timer), in the middle of the loop.
    if (const auto due = rotation_.switch_before_loop_end(now(), remaining, length); due && next_) {
        if (const auto next = next_(); next && *next != *on.video) {
            wait.player->load(next->file, next->loop, true, next->compatible);
            wait.video = next;
            waiting_is_switch_ = true;
            mid_loop_switch_ = true;
            arm_switch(*due);
            log(L"preloading next video (on time): " + name_of(*next));
            return;
        }
        // Only one video: nothing to switch to. Count again from now and wait for the loop end.
        rotation_.start_video(now(), paused_);
        arm_preload();
        return;
    }

    // Paused time does not count towards the interval, so this decision, made a little before
    // the loop end, is the same one that would be made at the loop end.
    std::optional<VideoSource> target;
    if (next_ && rotation_.switch_at_loop_end(now(), remaining)) {
        target = next_();
    }
    waiting_is_switch_ = target.has_value() && *target != *on.video;  // one video: just loop
    if (!waiting_is_switch_) {
        target = on.video;
    }

    if (wait.video == target && wait.player->loaded()) {
        wait.player->rewind();  // already has this loop (short loops stay loaded): just seek
    } else {
        wait.player->load(target->file, target->loop, true, target->compatible);
    }
    wait.video = target;
    log(waiting_is_switch_ ? L"preloading next video: " + name_of(*target)
                           : std::wstring(L"preloading the loop start"));
}

void VideoEngine::swap() {
    Slot& from = shown();
    Slot& to = waiting();
    // Order matters: start the waiting player, make it opaque, and only then make the old one
    // transparent. For an instant both are opaque (the one higher in z-order shows), never
    // neither.
    to.player->set_paused(paused_);
    to.window->reveal();
    from.window->hide();
    shown_ = 1 - shown_;

    if (waiting_is_switch_) {
        rotation_.start_video(now(), paused_);
        failures_ = 0;
        log(L"switched to " + name_of(*to.video));
    } else {
        log(L"loop restart");
    }
    waiting_is_switch_ = false;
    switch_now_ = false;
    mid_loop_switch_ = false;
    timer_is_switch_ = false;
    ::KillTimer(controller_, preload_timer_);  // e.g. a switch timer when the loop end came first
    timer_armed_ = false;

    // The old player is now the waiting one. Free it, unless the new loop is too short for that.
    Slot& old = waiting();
    const std::optional<double> length = shown().player->loop_length();
    if (!length || Rotation::keep_waiting_loaded(*length)) {
        prepare_waiting();  // rewinds if it already holds the right loop
    } else {
        old.player->unload();
        old.video.reset();
        if (!paused_) {
            arm_preload();
        }
    }
}

void VideoEngine::handle_errors() {
    Slot& on = shown();
    Slot& wait = waiting();

    // The first video (or the one after a failed one) could not be played.
    if (on.player->last_error() && !on.player->ready() && on.video) {
        log(L"cannot play " + name_of(*on.video) + L": " + to_wide(*on.player->last_error()));
        std::optional<VideoSource> next;
        if (next_ && ++failures_ < kMaxFailures) {
            next = next_();
        }
        if (next && *next != *on.video) {
            on.video = next;
            on.player->load(next->file, next->loop, paused_, next->compatible);
            rotation_.start_video(now(), paused_);
            log(L"playing " + name_of(*next));
        } else {
            on.player->unload();
            on.video.reset();
            gave_up_ = true;
            log(L"nothing playable: the normal wallpaper stays");
        }
        return;
    }

    // The next video failed while preloading: skip it. The shown one keeps holding its last
    // frame if the loop has already ended, so nothing visible goes wrong.
    if (wait.video && wait.player->last_error() && !wait.player->ready()) {
        log(L"cannot play " + name_of(*wait.video) + L", skipping it: " +
            to_wide(*wait.player->last_error()));
        wait.player->unload();
        wait.video.reset();
        switch_now_ = false;  // a forced switch to a broken file is dropped
        if (++failures_ >= kMaxFailures) {
            next_ = nullptr;  // stop rotating, keep looping what plays
            log(L"too many videos failed: rotation stopped");
        }
        prepare_waiting();
    }
}

}  // namespace wallflow::win
