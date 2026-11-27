#pragma once

#include <optional>
#include <string_view>

namespace wallflow {

// What the user asked for: loop from `start` to `end` (seconds into the video). Either may be
// missing: no start means the beginning, no end means the end of the file.
struct LoopSetting {
    std::optional<double> start;
    std::optional<double> end;
};

// The range that is actually played, after checking the setting against the video.
// `end` empty = play to the end of the file.
struct LoopRange {
    double start = 0;
    std::optional<double> end;

    friend bool operator==(const LoopRange&, const LoopRange&) = default;
};

// Shortest loop accepted. A shorter range is almost certainly a typo, and swapping players
// several times a second would look like flicker, not a loop.
inline constexpr double kMinLoopSeconds = 0.5;

// Turns the user's setting into a range that can be played. `duration` is the video's length
// if mpv knows it (it does not for some streams).
//
// Rules (a bad setting never stops the video from playing; it falls back to the whole file):
// - a missing, negative or non-finite start becomes 0;
// - an end at or past the end of the file becomes "to the end" (no seek needed there);
// - a start at or past the end of the file, or an end that is not at least kMinLoopSeconds
//   after the start, makes the whole file loop.
LoopRange resolve_loop(const LoopSetting& setting, std::optional<double> duration);

// Parses a time typed by the user: "90", "1:30", "1:02:03.5" or "12.25" (seconds, minutes:seconds
// or hours:minutes:seconds; only the last part may have a fraction). Returns nothing if the
// text is not a valid time.
std::optional<double> parse_time(std::wstring_view text);

}  // namespace wallflow
