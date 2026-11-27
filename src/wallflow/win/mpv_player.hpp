#pragma once

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "wallflow/loop_range.hpp"

struct mpv_handle;  // from <mpv/client.h>; declared here so users of this header don't need it

namespace wallflow::win {

// One libmpv player. It decodes on its own threads and draws into `window`; this class only
// starts it, gives it commands and reads its events. Nothing here blocks.
//
// Threading: create, use and destroy it on the UI thread. `wakeup` is called from an mpv
// thread whenever events are waiting; it must do nothing but post a message to the UI
// thread, which then calls process_events().
class MpvPlayer {
public:
    // `window` == nullptr creates a player with no video output (used by the tests).
    MpvPlayer(HWND window, std::function<void()> wakeup);
    ~MpvPlayer();

    MpvPlayer(const MpvPlayer&) = delete;
    MpvPlayer& operator=(const MpvPlayer&) = delete;

    // Loads `file` and plays the `loop` part of it: starts at loop.start (an exact seek, not
    // the nearest keyframe) and stops at loop.end, or at the end of the file. At the end mpv does
    // not unload the file: it pauses on the last frame and at_end() becomes true, so the
    // picture never goes black. `paused` = load and show the first frame, but do not play.
    //
    // The range is checked against the video's real length once the file is loaded (see
    // resolve_loop); range() then returns what is actually played.
    //
    // `compatible`: decode this file with Vulkan (copied back to memory) instead of DirectX.
    // Some GPU drivers decode some videos wrongly through DirectX (blocks of wrong colours);
    // this costs a little more CPU and memory but shows the right picture.
    void load(const std::filesystem::path& file, const LoopSetting& loop, bool paused,
              bool compatible = false);

    // Same with an mpv URL such as "av://lavfi:testsrc" (a generated test pattern).
    void load_url(const std::string& url, const LoopSetting& loop, bool paused, bool compatible = false);

    // Goes back to the start of the range (exact seek). The pause state does not change, so a
    // player that reached the end stays paused on the start frame until set_paused(false).
    void rewind();

    // Pauses or resumes playback. While paused mpv decodes nothing; the last frame stays shown.
    void set_paused(bool paused);

    // Muted: no audio track at all (not even decoded). Otherwise the video's audio at
    // `volume` (0 to 100). Applies now and to every file loaded later.
    void set_audio(bool muted, int volume);

    // Any mpv property as text, e.g. "aid" or "volume"; nothing if it is unavailable.
    // For tests and troubleshooting.
    [[nodiscard]] std::optional<std::string> property(const char* name) const;
    [[nodiscard]] bool paused() const;

    // Handles every waiting event without blocking.
    void process_events();

    // How many files have finished loading.
    [[nodiscard]] int files_loaded() const noexcept { return files_loaded_; }

    // True once the frame at the target position is on screen, after load() or rewind()
    // (mpv's playback-restart event). Until then the window would show an old or empty frame.
    [[nodiscard]] bool ready() const noexcept { return ready_; }

    // True while the player is held on the last frame of the range.
    [[nodiscard]] bool at_end() const noexcept { return at_end_; }

    [[nodiscard]] const std::optional<std::string>& last_error() const noexcept { return last_error_; }

    // The range being played (valid once files_loaded() > 0).
    [[nodiscard]] const LoopRange& range() const noexcept { return range_; }

    // Current playback position in seconds, if a file is playing.
    [[nodiscard]] std::optional<double> position() const;

    // Seconds of playback left until the end of the range, and the range's length. Known once
    // the file is loaded (and, for a range with no end, only if mpv knows the video's length).
    [[nodiscard]] std::optional<double> remaining() const;
    [[nodiscard]] std::optional<double> loop_length() const;

    // Unloads the file, which frees the decoder and its frames. The window keeps whatever it
    // showed last, so only do this while it is invisible.
    void unload();

    // True from load() until unload().
    [[nodiscard]] bool loaded() const noexcept { return loaded_; }

private:
    struct Destroy {
        void operator()(mpv_handle* mpv) const noexcept;
    };

    void on_file_loaded();
    // Sends a command that moves playback (load or seek); see ready().
    void send_target_command(const char** command);

    std::unique_ptr<mpv_handle, Destroy> mpv_;
    std::function<void()> wakeup_;
    int files_loaded_ = 0;
    bool ready_ = false;
    std::uint64_t last_id_ = 0;    // ids for async commands; 0 = "no id"
    std::uint64_t target_id_ = 0;  // the last load/seek
    bool target_confirmed_ = false;  // mpv has replied to it
    bool at_end_ = false;
    LoopSetting setting_;  // what load() was asked for
    LoopRange range_;      // what is played
    std::optional<double> duration_;  // the video's length, once loaded and if known
    bool loaded_ = false;
    std::optional<std::string> last_error_;
};

}  // namespace wallflow::win
