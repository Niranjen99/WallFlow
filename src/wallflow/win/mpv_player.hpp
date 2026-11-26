#pragma once

#include <windows.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

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

    // Starts playing `file` in an endless loop, muted, filling the window.
    void play(const std::filesystem::path& file);

    // Plays an mpv URL such as "av://lavfi:testsrc" (a generated test pattern).
    void play_url(const std::string& url);

    // Pauses or resumes playback. While paused mpv decodes nothing; the last frame stays shown.
    void set_paused(bool paused);
    [[nodiscard]] bool paused() const;

    // Handles every waiting event without blocking.
    void process_events();

    // How many files have finished loading, and the last playback error (if any).
    [[nodiscard]] int files_loaded() const noexcept { return files_loaded_; }

    // True once playback has really started (mpv's playback-restart event): the first frame
    // is ready, so the window can be shown without flashing.
    [[nodiscard]] bool started() const noexcept { return started_; }
    [[nodiscard]] const std::optional<std::string>& last_error() const noexcept { return last_error_; }

    // Current playback position in seconds, if a file is playing.
    [[nodiscard]] std::optional<double> position() const;

private:
    struct Destroy {
        void operator()(mpv_handle* mpv) const noexcept;
    };

    std::unique_ptr<mpv_handle, Destroy> mpv_;
    std::function<void()> wakeup_;
    int files_loaded_ = 0;
    bool started_ = false;
    std::optional<std::string> last_error_;
};

}  // namespace wallflow::win
