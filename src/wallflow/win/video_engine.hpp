#pragma once

#include <windows.h>

#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

#include "wallflow/desktop_layout.hpp"
#include "wallflow/loop_range.hpp"
#include "wallflow/rotation.hpp"
#include "wallflow/win/mpv_player.hpp"
#include "wallflow/win/wallpaper_window.hpp"

namespace wallflow::win {

// One video and the part of it to loop.
struct VideoSource {
    std::filesystem::path file;
    LoopSetting loop;
    bool compatible = false;  // decode with Vulkan instead of DirectX (see MpvPlayer::load)

    friend bool operator==(const VideoSource&, const VideoSource&) = default;
};

// Two wallpaper windows on top of each other, each with its own player. One is shown and
// plays. Shortly before its loop ends, the other one (invisible, opacity 0) loads what comes
// next and waits, paused, on its first frame: the loop start of the same video, or the next
// video when it is time to switch.
//
// At the loop end the shown player holds its last frame. The engine starts the waiting player
// and swaps the two windows' opacity in one go, so the screen goes from the last frame straight
// to the first: no black frame, no stall while a seek decodes from the previous keyframe.
// The old player then unloads, which frees its decoder memory until the next preload. Loops
// too short for that (see Rotation::keep_waiting_loaded) keep both players loaded.
//
// Threading: UI thread only. Both players post `player_message` to `controller`, and the
// preload timer `preload_timer` fires there as WM_TIMER; the app calls on_player_message()
// and on_preload_timer() for them.
class VideoEngine {
public:
    // Returns the next video to play, or nothing if there is none.
    using NextVideo = std::function<std::optional<VideoSource>()>;

    VideoEngine(HINSTANCE instance, HWND controller, UINT player_message, UINT_PTR preload_timer);
    ~VideoEngine();

    VideoEngine(const VideoEngine&) = delete;
    VideoEngine& operator=(const VideoEngine&) = delete;

    // Puts both windows behind the desktop icons (again, e.g. after a resolution change).
    // Returns nothing if the desktop is not ready yet.
    std::optional<DesktopLayout> attach();

    // False once a window is gone (Explorer restarted and took it down).
    [[nodiscard]] bool alive() const noexcept;

    // Starts looping `first`, paused if `paused`. With an `interval` and a `next` function it
    // moves on to next() after that much playing time (at a loop end); otherwise it loops
    // `first` forever. Creates the players on first use.
    void play(const VideoSource& first, bool paused, std::optional<double> interval,
              NextVideo next);

    // Pauses or resumes what is on screen. The waiting player always stays paused.
    void set_paused(bool paused);

    // Audio for both players, now and for every video after (see MpvPlayer::set_audio).
    void set_audio(bool muted, int volume);

    // Changes the rotation while playing (settings changed). The time played so far counts.
    void set_rotation(std::optional<double> interval, NextVideo next);

    // Switches to `video` as soon as it is ready, without waiting for the loop end. The same
    // seamless swap: it loads into the waiting player first. Used when the settings change
    // what should play (another folder, a new loop range for the video on screen).
    void switch_now(const VideoSource& video);

    // The video on screen (or loading for the screen), if any.
    [[nodiscard]] std::optional<VideoSource> current() const;

    void on_player_message();
    void on_preload_timer();

private:
    // Members are destroyed in reverse order: the player (which draws into the window) goes
    // before the window.
    struct Slot {
        std::unique_ptr<WallpaperWindow> window;
        std::unique_ptr<MpvPlayer> player;
        std::optional<VideoSource> video;  // what the player has loaded
    };

    Slot& shown() { return slots_[shown_]; }
    Slot& waiting() { return slots_[1 - shown_]; }

    // Starts the preload timer for the shown loop, or loads at once if there is no time.
    void arm_preload();
    // Decides what comes after the shown loop and gets the waiting player ready for it.
    void prepare_waiting();
    void swap();
    // The shown (or waiting) player could not load its video: try the next one.
    void handle_errors();

    HWND controller_;
    UINT player_message_;
    UINT_PTR preload_timer_;
    std::array<Slot, 2> slots_;
    int shown_ = 0;  // index of the slot on screen
    bool paused_ = false;
    bool playing_ = false;   // play() was called
    bool timer_armed_ = false;
    bool waiting_is_switch_ = false;  // the waiting player holds the next video
    bool switch_now_ = false;         // swap as soon as the waiting player is ready
    bool muted_ = true;
    int volume_ = 50;
    int failures_ = 0;       // videos in a row that failed to load
    bool gave_up_ = false;   // nothing playable: stop trying
    Rotation rotation_{std::nullopt};
    NextVideo next_;
};

}  // namespace wallflow::win
