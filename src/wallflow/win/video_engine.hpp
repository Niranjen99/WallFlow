#pragma once

#include <windows.h>

#include <array>
#include <filesystem>
#include <memory>
#include <optional>

#include "wallflow/desktop_layout.hpp"
#include "wallflow/loop_range.hpp"
#include "wallflow/win/mpv_player.hpp"
#include "wallflow/win/wallpaper_window.hpp"

namespace wallflow::win {

// Two wallpaper windows on top of each other, each with its own player. One is shown and
// plays; the other is invisible (opacity 0) and waits, paused, on the first frame of what
// comes next. For a loop that is the loop start of the same video.
//
// When the shown player reaches the end of the loop it holds its last frame. The engine then
// starts the waiting player and swaps the two windows' opacity in one go, so the screen goes
// from the last frame straight to the first: no black frame, no stall while a seek decodes
// from the previous keyframe. The old player then rewinds, invisibly, and becomes the one
// that waits. Folder rotation (Step 5) will use the same swap with a different video.
//
// Threading: UI thread only. Both players post `player_message` to `controller`; the app
// calls on_player_message() for it.
class VideoEngine {
public:
    VideoEngine(HINSTANCE instance, HWND controller, UINT player_message);

    VideoEngine(const VideoEngine&) = delete;
    VideoEngine& operator=(const VideoEngine&) = delete;

    // Puts both windows behind the desktop icons (again, e.g. after a resolution change).
    // Returns nothing if the desktop is not ready yet.
    std::optional<DesktopLayout> attach();

    // False once a window is gone (Explorer restarted and took it down).
    [[nodiscard]] bool alive() const noexcept;

    // Starts looping `loop` of `video`, paused if `paused`. Creates the players on first use.
    void play(const std::filesystem::path& video, const LoopSetting& loop, bool paused);

    // Pauses or resumes what is on screen. The waiting player always stays paused.
    void set_paused(bool paused);

    // Handles both players' events and swaps them when the loop ends.
    void on_player_message();

    // Number of loop restarts so far.
    [[nodiscard]] int swaps() const noexcept { return swaps_; }

private:
    // Members are destroyed in reverse order: the player (which draws into the window) goes
    // before the window.
    struct Slot {
        std::unique_ptr<WallpaperWindow> window;
        std::unique_ptr<MpvPlayer> player;
    };

    Slot& shown() { return slots_[shown_]; }
    Slot& waiting() { return slots_[1 - shown_]; }
    void swap();

    HWND controller_;
    UINT player_message_;
    std::array<Slot, 2> slots_;
    int shown_ = 0;  // index of the slot on screen
    bool paused_ = false;
    bool playing_ = false;  // play() was called
    int swaps_ = 0;
};

}  // namespace wallflow::win
