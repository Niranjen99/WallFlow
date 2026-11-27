#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "wallflow/settings.hpp"

namespace wallflow {

// The settings window's logic, without any window: what a typed field means, and how an edit
// changes the settings. Kept here so it can be unit tested.

// A time as the window shows it: 90 -> L"1:30". Nothing -> empty text.
std::wstring time_text(double seconds);
std::wstring time_text(std::optional<double> seconds);
// A number of seconds as plain text: 300 -> L"300", 2.5 -> L"2.5".
std::wstring seconds_text(double seconds);

// The "play each video for ... seconds" field: seconds (300), or minutes:seconds (5:00) also
// works. Either `seconds` is set, or `error` says what is wrong.
struct IntervalField {
    std::optional<double> seconds;
    std::wstring error;
};
IntervalField read_interval(std::wstring_view text);

// The loop start and end fields of one video. An empty field means "not set" (start of the
// video / end of the video); both empty means the whole video loops.
struct LoopFields {
    std::optional<LoopSetting> loop;
    std::wstring error;
};
LoopFields read_loop(std::wstring_view start, std::wstring_view end);

// Sets the loop of the video named `file_name`, replacing an entry that differs only in case
// ("Clip.MP4" vs "clip.mp4"). A loop with neither start nor end removes the entry.
void set_loop(Settings& settings, const std::wstring& file_name, const LoopSetting& loop);

// One line of the window's video list.
struct VideoRow {
    std::wstring name;   // file name, e.g. L"goku.mp4"
    std::wstring start;  // loop start as text, empty = from the beginning
    std::wstring end;    // loop end as text, empty = to the end
};
std::vector<VideoRow> video_rows(const std::vector<std::filesystem::path>& videos,
                                 const Settings& settings);

}  // namespace wallflow
