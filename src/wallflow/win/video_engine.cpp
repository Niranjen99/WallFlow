#include "wallflow/win/video_engine.hpp"

#include <string>

#include "wallflow/win/log.hpp"

namespace wallflow::win {

VideoEngine::VideoEngine(HINSTANCE instance, HWND controller, UINT player_message)
    : controller_(controller), player_message_(player_message) {
    for (Slot& slot : slots_) {
        slot.window = std::make_unique<WallpaperWindow>(instance);
    }
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

void VideoEngine::play(const std::filesystem::path& video, const LoopSetting& loop, bool paused) {
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
    paused_ = paused;
    playing_ = true;
    // Both load the same range. The waiting one stays paused on the loop start.
    shown().player->load(video, loop, paused);
    waiting().player->load(video, loop, true);
}

void VideoEngine::set_paused(bool paused) {
    paused_ = paused;
    if (playing_) {
        shown().player->set_paused(paused);
    }
}

void VideoEngine::on_player_message() {
    if (!playing_) {
        return;
    }
    for (Slot& slot : slots_) {
        slot.player->process_events();
        if (const auto& error = slot.player->last_error()) {
            log(L"playback error: " + to_wide(*error));
        }
    }

    Slot& on_screen = shown();
    if (!on_screen.window->revealed() && on_screen.player->ready()) {
        on_screen.window->reveal();
        log(L"first frame ready: wallpaper revealed");
    }
    // The shown player holds its last frame until the waiting one is ready, so a slow
    // rewind only lengthens the last frame; it never shows black.
    if (on_screen.player->at_end() && waiting().player->ready()) {
        swap();
    }
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
    // At the end mpv paused the old player by itself; rewind leaves it paused at the start.
    from.player->rewind();
    shown_ = 1 - shown_;
    ++swaps_;
    log(L"loop restart " + std::to_wstring(swaps_) + L": now on player " +
        std::to_wstring(shown_));
}

}  // namespace wallflow::win
