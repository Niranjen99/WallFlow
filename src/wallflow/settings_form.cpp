#include "wallflow/settings_form.hpp"

#include <cmath>
#include <cwctype>
#include <iterator>

#include "wallflow/loop_range.hpp"

namespace wallflow {

namespace {

std::wstring_view trim(std::wstring_view text) {
    while (!text.empty() && std::iswspace(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::iswspace(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

bool same_name(std::wstring_view a, std::wstring_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::towlower(a[i]) != std::towlower(b[i])) {
            return false;
        }
    }
    return true;
}

constexpr wchar_t kTimeHelp[] = L"Use seconds (90 or 4.5) or minutes:seconds (1:30).";

}  // namespace

std::wstring time_text(double seconds) {
    const std::string text = format_time(seconds);  // ASCII only: digits, ':' and '.'
    return std::wstring(text.begin(), text.end());
}

std::wstring time_text(std::optional<double> seconds) {
    return seconds ? time_text(*seconds) : std::wstring();
}

std::wstring seconds_text(double seconds) {
    // Whole milliseconds, then trailing zeros dropped: 2.5 -> "2.5", 300 -> "300".
    const auto ms = static_cast<long long>(std::llround(seconds * 1000));
    std::wstring text = std::to_wstring(ms / 1000);
    if (ms % 1000 != 0) {
        std::wstring fraction = std::to_wstring(1000 + ms % 1000).substr(1);  // 5 -> "005"
        while (fraction.back() == L'0') {
            fraction.pop_back();
        }
        text += L"." + fraction;
    }
    return text;
}

IntervalField read_interval(std::wstring_view text) {
    text = trim(text);
    if (text.empty()) {
        return {std::nullopt, L"Enter how many seconds each video plays, for example 300."};
    }
    const std::optional<double> seconds = parse_time(text);
    if (!seconds) {
        return {std::nullopt, L"Not a number of seconds. Type for example 30 or 300."};
    }
    if (*seconds < kMinInterval) {
        return {std::nullopt, L"Each video plays at least 10 seconds."};
    }
    return {seconds, {}};
}

LoopFields read_loop(std::wstring_view start_text, std::wstring_view end_text) {
    start_text = trim(start_text);
    end_text = trim(end_text);
    LoopSetting loop;
    if (!start_text.empty()) {
        loop.start = parse_time(start_text);
        if (!loop.start) {
            return {std::nullopt, std::wstring(L"The loop start is not a time. ") + kTimeHelp};
        }
    }
    if (!end_text.empty()) {
        loop.end = parse_time(end_text);
        if (!loop.end) {
            return {std::nullopt, std::wstring(L"The loop end is not a time. ") + kTimeHelp};
        }
    }
    // Same rule as resolve_loop(), told to the user instead of silently looping the whole video.
    // (Whether the times fit inside the video is only known once it plays.)
    if (loop.start && loop.end && *loop.end < *loop.start + kMinLoopSeconds) {
        return {std::nullopt, L"The loop end must be at least half a second after the start."};
    }
    return {loop, {}};
}

void set_loop(Settings& settings, const std::wstring& file_name, const LoopSetting& loop) {
    // std::erase_if on a map (C++20): removes every entry the lambda says yes to.
    std::erase_if(settings.loops, [&](const auto& entry) { return same_name(entry.first, file_name); });
    if (loop.start || loop.end) {
        settings.loops[file_name] = loop;
    }
}

void set_compatible(Settings& settings, const std::wstring& file_name, bool on) {
    std::erase_if(settings.compatible_decoding, [&](const std::wstring& name) { return same_name(name, file_name); });
    if (on) {
        settings.compatible_decoding.insert(file_name);
    }
}

std::vector<VideoRow> video_rows(const std::vector<std::filesystem::path>& videos,
                                 const Settings& settings) {
    std::vector<VideoRow> rows;
    rows.reserve(videos.size());
    for (const std::filesystem::path& video : videos) {
        const LoopSetting loop = settings.loop_for(video);
        const std::wstring name = video.filename().wstring();
        rows.push_back({name, time_text(loop.start), time_text(loop.end),
                        settings.compatible_for(video), same_name(name, settings.only_video)});
    }
    return rows;
}

std::wstring options_text(const VideoRow& row) {
    if (row.only && row.compatible) {
        return L"Only, compatible";
    }
    if (row.only) {
        return L"Only this";
    }
    return row.compatible ? L"Compatible" : L"";
}

}  // namespace wallflow
