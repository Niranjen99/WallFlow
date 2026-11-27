// Measures how long the picture stands still at a loop point, for two ways of looping:
//
//   two-player  WallFlow's engine: a second player waits, paused, on the loop start, and
//               the two swap at the end (the real MpvPlayer code, swapped like VideoEngine).
//   ab-loop     mpv's built-in A-B loop: one player that seeks back to the start at the end.
//
// For each loop it prints the overrun: how much longer the loop took than its length, which
// is the time the last frame stays on screen too long. The length is counted in whole frames
// (frames whose time is in [start, end)), since a loop can only show whole frames; this
// assumes a constant frame rate, which the program reads from the file. For ab-loop it also prints the seek
// stall (seek started -> first frame at the start ready).
//
// The players decode (on the GPU, as in the app) but draw nothing (vo=null), so the numbers
// do not include the compositor. Run it on an idle PC:
//
//   loop_bench.exe <video> <start> <end> [loops]      times as in WallFlow: 90, 1:30, 2.5

#include <mpv/client.h>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <format>
#include <functional>
#include <mutex>
#include <numeric>
#include <string>
#include <vector>

#include "wallflow/loop_range.hpp"
#include "wallflow/win/mpv_player.hpp"

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
using wallflow::win::MpvPlayer;

namespace {

double ms_since(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

// Wakes the main thread when any player has events (like PostMessage in the app).
class Pump {
public:
    std::function<void()> wakeup() {
        return [this] {
            {
                std::lock_guard lock(mutex_);
                pending_ = true;
            }
            cv_.notify_one();
        };
    }
    void wait() {
        std::unique_lock lock(mutex_);
        cv_.wait_for(lock, 100ms, [this] { return pending_; });
        pending_ = false;
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    bool pending_ = false;
};

void print_summary(const char* name, const std::vector<double>& values) {
    if (values.empty()) {
        std::printf("%-28s no data\n", name);
        return;
    }
    std::vector<double> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    const double mean = std::accumulate(sorted.begin(), sorted.end(), 0.0) / sorted.size();
    std::printf("%-28s mean %6.1f ms   median %6.1f ms   max %6.1f ms   (%zu loops)\n", name,
                mean, sorted[sorted.size() / 2], sorted.back(), sorted.size());
}

// The file's frame rate (container-fps), or 0 if unknown.
double probe_fps(const std::string& url) {
    mpv_handle* mpv = mpv_create();
    mpv_set_option_string(mpv, "config", "no");
    mpv_set_option_string(mpv, "vo", "null");
    mpv_set_option_string(mpv, "pause", "yes");
    mpv_initialize(mpv);
    const char* command[] = {"loadfile", url.c_str(), nullptr};
    mpv_command(mpv, command);
    double fps = 0;
    for (;;) {
        const mpv_event* event = mpv_wait_event(mpv, 10);
        if (event->event_id == MPV_EVENT_FILE_LOADED) {
            mpv_get_property(mpv, "container-fps", MPV_FORMAT_DOUBLE, &fps);
            break;
        }
        if (event->event_id == MPV_EVENT_END_FILE || event->event_id == MPV_EVENT_NONE) {
            break;
        }
    }
    mpv_terminate_destroy(mpv);
    return fps;
}

std::string to_utf8(const std::wstring& video) {
    const std::u8string utf8 = std::filesystem::path(video).u8string();
    return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
}

// WallFlow's way. Returns the overrun of each loop, in ms.
std::vector<double> run_two_player(const std::wstring& video, double start, double end,
                                   double length, int loops) {
    Pump pump;
    MpvPlayer players[2] = {MpvPlayer(nullptr, pump.wakeup()), MpvPlayer(nullptr, pump.wakeup())};
    const wallflow::LoopSetting loop{start, end};
    players[0].load(video, loop, false);
    players[1].load(video, loop, true);

    int shown = 0;
    std::vector<double> overruns;
    Clock::time_point last_swap{};
    bool started = false;
    const auto deadline = Clock::now() + std::chrono::duration<double>((end - start) * (loops + 3));
    while (static_cast<int>(overruns.size()) < loops && Clock::now() < deadline) {
        pump.wait();
        players[0].process_events();
        players[1].process_events();
        MpvPlayer& on = players[shown];
        MpvPlayer& next = players[1 - shown];
        if (!started && on.ready()) {
            started = true;
            last_swap = Clock::now();  // the loop starts playing now
        }
        if (started && on.at_end() && next.ready()) {
            next.set_paused(false);
            on.rewind();
            shown = 1 - shown;
            const auto now = Clock::now();
            overruns.push_back(ms_since(last_swap, now) - length * 1000);
            last_swap = now;
        }
    }
    return overruns;
}

// mpv's own A-B loop on one player, with the same options as MpvPlayer.
void run_ab_loop(const std::wstring& video, double start, double end, double length,
                 int loops, std::vector<double>& overruns, std::vector<double>& stalls) {
    mpv_handle* mpv = mpv_create();
    for (const auto& [name, value] :
         {std::pair{"config", "no"}, {"terminal", "no"}, {"idle", "yes"}, {"vo", "null"},
          {"hwdec", "auto-safe"}, {"aid", "no"}, {"hr-seek", "yes"}}) {
        mpv_set_option_string(mpv, name, value);
    }
    mpv_set_option_string(mpv, "ab-loop-a", std::format("{}", start).c_str());
    mpv_set_option_string(mpv, "ab-loop-b", std::format("{}", end).c_str());
    mpv_initialize(mpv);

    const std::string url = to_utf8(video);
    const std::string options = std::format("start={}", start);
    const char* command[] = {"loadfile", url.c_str(), "replace", "-1", options.c_str(), nullptr};
    mpv_command(mpv, command);

    Clock::time_point last_restart{};
    Clock::time_point seek_at{};
    bool started = false;
    bool seeking = false;
    const auto deadline = Clock::now() + std::chrono::duration<double>((end - start) * (loops + 3));
    while (static_cast<int>(overruns.size()) < loops && Clock::now() < deadline) {
        const mpv_event* event = mpv_wait_event(mpv, 0.1);
        const auto now = Clock::now();
        if (event->event_id == MPV_EVENT_SEEK && started) {
            seek_at = now;
            seeking = true;
        } else if (event->event_id == MPV_EVENT_PLAYBACK_RESTART) {
            if (!started) {
                started = true;
            } else if (seeking) {
                stalls.push_back(ms_since(seek_at, now));
                overruns.push_back(ms_since(last_restart, now) - length * 1000);
                seeking = false;
            }
            last_restart = now;
        }
    }
    mpv_terminate_destroy(mpv);
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 4) {
        std::printf("usage: loop_bench <video> <start> <end> [loops]\n");
        return 2;
    }
    const auto start = wallflow::parse_time(argv[2]);
    const auto end = wallflow::parse_time(argv[3]);
    const int loops = argc > 4 ? std::stoi(argv[4]) : 10;
    if (!start || !end || *end - *start < wallflow::kMinLoopSeconds || loops < 1) {
        std::printf("bad start/end/loops\n");
        return 2;
    }
    // Frames shown in one loop: from the first frame at or after start to the last one before
    // end. Without a known frame rate, fall back to end - start.
    const double fps = probe_fps(to_utf8(argv[1]));
    double length = *end - *start;
    if (fps > 0) {
        const double first = std::ceil(*start * fps - 1e-6);
        const double after_last = std::ceil(*end * fps - 1e-6);
        length = (after_last - first) / fps;
    }
    std::printf("loop %.3f s .. %.3f s, %.3f fps, loop length %.1f ms (whole frames), %d loops\n\n",
                *start, *end, fps, length * 1000, loops);

    const auto two = run_two_player(argv[1], *start, *end, length, loops);
    std::vector<double> ab_overruns;
    std::vector<double> ab_stalls;
    run_ab_loop(argv[1], *start, *end, length, loops, ab_overruns, ab_stalls);

    std::printf("%-6s %18s %18s %18s\n", "loop", "two-player overrun", "ab-loop overrun",
                "ab-loop seek stall");
    for (int i = 0; i < loops; ++i) {
        auto cell = [i](const std::vector<double>& v) {
            return i < static_cast<int>(v.size()) ? std::format("{:.1f} ms", v[i]) : std::string("-");
        };
        std::printf("%-6d %18s %18s %18s\n", i + 1, cell(two).c_str(), cell(ab_overruns).c_str(),
                    cell(ab_stalls).c_str());
    }
    std::printf("\n");
    print_summary("two-player overrun", two);
    print_summary("ab-loop overrun", ab_overruns);
    print_summary("ab-loop seek stall", ab_stalls);
    return 0;
}
