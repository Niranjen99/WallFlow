#include "wallflow/command_line.hpp"

namespace wallflow {

CommandLine parse_command_line(std::span<const std::wstring> args) {
    CommandLine result;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::wstring& arg = args[i];
        if (arg == L"--start" || arg == L"--end") {
            if (i + 1 == args.size()) {
                result.error = arg + L" needs a time, e.g. " + arg + L" 1:30";
                return result;
            }
            const std::wstring& value = args[++i];
            const auto seconds = parse_time(value);
            if (!seconds) {
                result.error = L"not a time: \"" + value + L"\" (use 90, 1:30 or 1:02:03.5)";
                return result;
            }
            (arg == L"--start" ? result.loop.start : result.loop.end) = seconds;
        } else if (arg.starts_with(L"--")) {
            result.error = L"unknown option " + arg;
            return result;
        } else if (result.video.empty()) {
            result.video = arg;
        } else {
            result.error = L"only one video can be given (\"" + arg + L"\" is a second one)";
            return result;
        }
    }
    return result;
}

}  // namespace wallflow
