#include "wallflow/win/mpv_player.hpp"

#include <mpv/client.h>

#include <cstdint>
#include <format>
#include <stdexcept>
#include <string_view>

namespace wallflow::win {

namespace {

void set_option(mpv_handle* mpv, const char* name, const char* value) {
    if (const int err = mpv_set_option_string(mpv, name, value); err < 0) {
        throw std::runtime_error(std::string("mpv option ") + name + ": " + mpv_error_string(err));
    }
}

}  // namespace

void MpvPlayer::Destroy::operator()(mpv_handle* mpv) const noexcept {
    // Stops playback, waits for mpv's threads to finish and frees everything.
    mpv_terminate_destroy(mpv);
}

MpvPlayer::MpvPlayer(HWND window, std::function<void()> wakeup)
    : mpv_(mpv_create()), wakeup_(std::move(wakeup)) {
    if (!mpv_) {
        throw std::runtime_error("mpv_create failed");
    }
    mpv_handle* mpv = mpv_.get();

    set_option(mpv, "config", "no");    // ignore any mpv.conf on this PC: same behaviour everywhere
    set_option(mpv, "terminal", "no");  // no console output
    set_option(mpv, "idle", "yes");     // stay alive with nothing loaded
    set_option(mpv, "hwdec", "auto-safe");  // decode on the GPU where it is known to work
    set_option(mpv, "aid", "no");       // no audio track at all: not even decoded (a setting later)
    // At the end of the file (or of the loop range) keep the last frame on screen, paused,
    // instead of unloading. The app decides what comes next; mpv never shows black.
    set_option(mpv, "keep-open", "always");
    // Seeks land on the exact frame asked for, not the keyframe before it. A loop start is
    // usually between keyframes.
    set_option(mpv, "hr-seek", "yes");
    set_option(mpv, "panscan", "1.0");  // fill the screen; crop instead of black bars
    set_option(mpv, "osd-level", "0");  // no on-screen text
    set_option(mpv, "input-default-bindings", "no");  // keys and mouse do nothing
    set_option(mpv, "input-vo-keyboard", "no");
    set_option(mpv, "cursor-autohide", "no");

    // Troubleshooting: set the environment variable WALLFLOW_MPV_LOG to a file path and mpv
    // writes its detailed log there (which decoder, hardware decoding, errors).
    char log_path[1024] = {};
    if (::GetEnvironmentVariableA("WALLFLOW_MPV_LOG", log_path, sizeof log_path) > 0) {
        set_option(mpv, "log-file", log_path);
    }

    if (window != nullptr) {
        // "wid": draw into this existing window instead of opening one. mpv wants the handle
        // as a 64-bit integer.
        auto wid = static_cast<std::int64_t>(reinterpret_cast<std::intptr_t>(window));
        if (const int err = mpv_set_option(mpv, "wid", MPV_FORMAT_INT64, &wid); err < 0) {
            throw std::runtime_error(std::string("mpv option wid: ") + mpv_error_string(err));
        }
    } else {
        set_option(mpv, "vo", "null");  // decode, but show nothing
    }

    if (const int err = mpv_initialize(mpv); err < 0) {
        throw std::runtime_error(std::string("mpv_initialize: ") + mpv_error_string(err));
    }

    // mpv calls this plain function from one of its own threads. The captureless lambda
    // converts to a function pointer; `this` travels as the void* context, like
    // GWLP_USERDATA for windows.
    mpv_set_wakeup_callback(
        mpv, [](void* self) { static_cast<MpvPlayer*>(self)->wakeup_(); }, this);

    // mpv sends a property-change event whenever "eof-reached" changes, so the end of the
    // range arrives as an event, not by polling the position.
    mpv_observe_property(mpv, 0, "eof-reached", MPV_FORMAT_FLAG);
}

MpvPlayer::~MpvPlayer() {
    if (mpv_) {
        // No more wakeups: after this object is gone the callback's `this` would dangle.
        mpv_set_wakeup_callback(mpv_.get(), nullptr, nullptr);
    }
}

void MpvPlayer::load(const std::filesystem::path& file, const LoopSetting& loop, bool paused) {
    // mpv takes UTF-8. A Windows path is UTF-16 inside std::filesystem::path; u8string()
    // converts it. C++20 returns std::u8string (char8_t), so the bytes are re-read as char.
    const std::u8string utf8 = file.u8string();
    load_url(std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size()), loop, paused);
}

void MpvPlayer::load_url(const std::string& url, const LoopSetting& loop, bool paused) {
    last_error_.reset();
    ready_ = false;
    at_end_ = false;
    setting_ = loop;
    // The length is not known yet: check what can be checked now, the rest in on_file_loaded().
    range_ = resolve_loop(loop, std::nullopt);

    // Per-file options travel with the loadfile command, so the very first frame decoded is
    // already the loop start (no flash of frame 0). std::format prints doubles the same way
    // on every PC (no locale), with no rounding.
    std::string options = std::format("start={}", range_.start);
    if (range_.end) {
        options += std::format(",end={}", *range_.end);
    }
    // Requests on one mpv handle run in order, so the pause is in place before the load.
    set_paused(paused);
    // loadfile <url> <flags> <index> <options>; index -1 = not used (only for "insert-at").
    const char* command[] = {"loadfile", url.c_str(), "replace", "-1", options.c_str(), nullptr};
    send_target_command(command);
}

void MpvPlayer::send_target_command(const char** command) {
    // A playback-restart event says "the frame at the new position is on screen", but it does
    // not say which command it belongs to: one from an earlier seek may still be waiting in
    // the queue. So each load/seek gets its own id, and restarts count only after mpv has
    // replied to that command (mpv handles commands, and queues their events, in order).
    ready_ = false;
    target_confirmed_ = false;
    target_id_ = ++last_id_;
    // mpv_command_async queues the command and returns at once; the reply arrives as an event
    // carrying the id passed here.
    mpv_command_async(mpv_.get(), target_id_, command);
}

void MpvPlayer::on_file_loaded() {
    double duration = 0;
    std::optional<double> known;
    if (mpv_get_property(mpv_.get(), "duration", MPV_FORMAT_DOUBLE, &duration) >= 0) {
        known = duration;
    }
    const LoopRange checked = resolve_loop(setting_, known);
    if (checked == range_) {
        return;
    }
    // Only a start at or near the end of the file gets here. Fall back to the whole file:
    // clear the end and jump to the start. The wrong frame may show for an instant; this only
    // happens with a bad setting.
    range_ = checked;
    mpv_set_property_string(mpv_.get(), "file-local-options/end", "none");
    rewind();
}

void MpvPlayer::rewind() {
    at_end_ = false;
    const std::string target = std::format("{}", range_.start);
    const char* command[] = {"seek", target.c_str(), "absolute+exact", nullptr};
    send_target_command(command);
}

void MpvPlayer::set_paused(bool paused) {
    // mpv's "flag" format is an int (0 or 1). The async call copies the value before
    // returning, so pointing at a local variable is fine.
    int flag = paused ? 1 : 0;
    mpv_set_property_async(mpv_.get(), 0, "pause", MPV_FORMAT_FLAG, &flag);
}

bool MpvPlayer::paused() const {
    int flag = 0;
    return mpv_get_property(mpv_.get(), "pause", MPV_FORMAT_FLAG, &flag) >= 0 && flag != 0;
}

void MpvPlayer::process_events() {
    for (;;) {
        // Timeout 0: return immediately with MPV_EVENT_NONE when nothing is waiting.
        const mpv_event* event = mpv_wait_event(mpv_.get(), 0);
        switch (event->event_id) {
            case MPV_EVENT_NONE:
                return;
            case MPV_EVENT_FILE_LOADED:
                ++files_loaded_;
                on_file_loaded();
                break;
            case MPV_EVENT_PLAYBACK_RESTART:
                if (target_confirmed_) {
                    ready_ = true;
                }
                break;
            case MPV_EVENT_PROPERTY_CHANGE: {
                // data points at an mpv_event_property that mpv owns until the next
                // mpv_wait_event call. Its data is null when the value is unavailable (no file).
                const auto* prop = static_cast<const mpv_event_property*>(event->data);
                if (std::string_view(prop->name) == "eof-reached") {
                    at_end_ = prop->format == MPV_FORMAT_FLAG &&
                              *static_cast<const int*>(prop->data) != 0;
                }
                break;
            }
            case MPV_EVENT_END_FILE: {
                const auto* end = static_cast<const mpv_event_end_file*>(event->data);
                if (end->reason == MPV_END_FILE_REASON_ERROR) {
                    last_error_ = mpv_error_string(end->error);
                }
                break;
            }
            case MPV_EVENT_COMMAND_REPLY:
                if (event->reply_userdata == target_id_) {
                    target_confirmed_ = true;
                }
                if (event->error < 0) {
                    last_error_ = mpv_error_string(event->error);
                }
                break;
            default:
                break;
        }
    }
}

std::optional<double> MpvPlayer::position() const {
    double seconds = 0;
    if (mpv_get_property(mpv_.get(), "time-pos", MPV_FORMAT_DOUBLE, &seconds) < 0) {
        return std::nullopt;
    }
    return seconds;
}

}  // namespace wallflow::win
