#include "wallflow/command_line.hpp"

namespace wallflow {

namespace {

// Shortest accepted rotation interval. Shorter is almost certainly a typo ("5" meant as
// minutes) and would reload videos all the time.
constexpr double kMinInterval = 10;

}  // namespace

CommandLine parse_command_line(std::span<const std::wstring> args) {
    CommandLine result;
    bool interval_given = false;
    auto fail = [&result](std::wstring message) {
        result.error = std::move(message);
        return result;
    };

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::wstring& arg = args[i];
        if (arg == L"--start" || arg == L"--end" || arg == L"--interval" || arg == L"--folder") {
            if (i + 1 == args.size()) {
                return fail(arg + L" needs a value");
            }
            const std::wstring& value = args[++i];
            if (arg == L"--folder") {
                result.folder = value;
                continue;
            }
            const auto seconds = parse_time(value);
            if (!seconds) {
                return fail(L"not a time: \"" + value + L"\" (use 90, 1:30 or 1:02:03.5)");
            }
            if (arg == L"--interval") {
                if (*seconds < kMinInterval) {
                    return fail(L"--interval must be at least 10 seconds (e.g. 5:00 for five minutes)");
                }
                result.interval = *seconds;
                interval_given = true;
            } else {
                (arg == L"--start" ? result.loop.start : result.loop.end) = seconds;
            }
        } else if (arg == L"--shuffle") {
            result.shuffle = true;
        } else if (arg.starts_with(L"--")) {
            return fail(L"unknown option " + arg);
        } else if (result.video.empty()) {
            result.video = arg;
        } else {
            return fail(L"only one video can be given (\"" + arg + L"\" is a second one)");
        }
    }

    if (!result.video.empty() && !result.folder.empty()) {
        return fail(L"give either a video or --folder, not both");
    }
    if (!result.folder.empty() && (result.loop.start || result.loop.end)) {
        return fail(L"--start and --end work with a single video only (for now)");
    }
    if (result.folder.empty() && (interval_given || result.shuffle)) {
        return fail(L"--interval and --shuffle need --folder");
    }
    return result;
}

}  // namespace wallflow
