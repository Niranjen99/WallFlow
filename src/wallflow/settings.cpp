#include "wallflow/settings.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <format>

namespace wallflow {

namespace {

using nlohmann::json;

// JSON strings are UTF-8; Windows paths and file names are UTF-16. std::filesystem::path
// converts between the two (char8_t strings are UTF-8 by definition).
std::wstring from_utf8(const std::string& text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end())).wstring();
}

std::string to_utf8(const std::wstring& text) {
    const std::u8string utf8 = std::filesystem::path(text).u8string();
    return std::string(utf8.begin(), utf8.end());
}

bool equal_ignoring_case(std::wstring_view a, std::wstring_view b) {
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

// A time is a number of seconds (90, 2.5) or text in parse_time's format ("1:30").
std::optional<double> read_time(const json& value) {
    if (value.is_number()) {
        const double seconds = value.get<double>();
        return std::isfinite(seconds) && seconds >= 0 ? std::optional(seconds) : std::nullopt;
    }
    if (value.is_string()) {
        return parse_time(from_utf8(value.get<std::string>()));
    }
    return std::nullopt;
}

// Reads one field into `out` if it is there and valid; otherwise adds a warning.
class Reader {
public:
    Reader(const json& object, std::vector<std::string>& warnings)
        : object_(object), warnings_(warnings) {}

    void flag(const char* key, bool& out) {
        if (const json* value = find(key)) {
            if (value->is_boolean()) {
                out = value->get<bool>();
            } else {
                warn(key, "must be true or false");
            }
        }
    }

    void path(const char* key, std::filesystem::path& out) {
        if (const json* value = find(key)) {
            if (value->is_string()) {
                out = from_utf8(value->get<std::string>());
            } else {
                warn(key, "must be text (a folder path)");
            }
        }
    }

    void interval(const char* key, double& out) {
        if (const json* value = find(key)) {
            const auto seconds = read_time(*value);
            if (seconds && *seconds >= kMinInterval) {
                out = *seconds;
            } else {
                warn(key, "must be a time of at least 10 seconds, e.g. \"5:00\"");
            }
        }
    }

    void volume(const char* key, int& out) {
        if (const json* value = find(key)) {
            if (value->is_number() && value->get<double>() >= 0 && value->get<double>() <= 100) {
                out = static_cast<int>(std::lround(value->get<double>()));
            } else {
                warn(key, "must be a number from 0 to 100");
            }
        }
    }

    void warn(const std::string& key, const std::string& problem) {
        warnings_.push_back("\"" + key + "\" " + problem + ": ignored");
    }

private:
    const json* find(const char* key) const {
        const auto it = object_.find(key);
        return it == object_.end() ? nullptr : &*it;
    }

    const json& object_;
    std::vector<std::string>& warnings_;
};

}  // namespace

LoopSetting Settings::loop_for(const std::filesystem::path& video) const {
    const std::wstring name = video.filename().wstring();
    for (const auto& [file, loop] : loops) {
        if (equal_ignoring_case(file, name)) {
            return loop;
        }
    }
    return {};
}

ParsedSettings parse_settings(std::string_view text) {
    ParsedSettings result;
    // parse(text, callback, allow_exceptions = false, ignore_comments = true): on bad JSON it
    // returns a "discarded" value instead of throwing.
    const json root = json::parse(text, nullptr, false, true);
    if (root.is_discarded() || !root.is_object()) {
        result.valid_json = false;
        result.warnings.push_back("the file is not valid JSON (an object { ... }): all ignored");
        return result;
    }

    Settings& s = result.settings;
    Reader read(root, result.warnings);
    read.path("folder", s.folder);
    read.interval("interval", s.interval);
    read.flag("shuffle", s.shuffle);
    read.flag("mute", s.mute);
    read.volume("volume", s.volume);
    read.flag("lock_screen_picture", s.lock_screen_picture);
    read.flag("start_with_windows", s.start_with_windows);

    if (const auto videos = root.find("videos"); videos != root.end()) {
        if (!videos->is_object()) {
            read.warn("videos", "must be an object { \"file.mp4\": { ... } }");
        } else {
            for (const auto& [name, entry] : videos->items()) {
                const std::string where = "videos/" + name;
                if (!entry.is_object()) {
                    read.warn(where, "must be an object { \"start\": ..., \"end\": ... }");
                    continue;
                }
                LoopSetting loop;
                for (const char* key : {"start", "end"}) {
                    if (const auto value = entry.find(key); value != entry.end()) {
                        if (const auto seconds = read_time(*value)) {
                            (key[0] == 's' ? loop.start : loop.end) = seconds;
                        } else {
                            read.warn(where + "/" + key, "must be a time like 90 or \"1:30\"");
                        }
                    }
                }
                if (loop.start || loop.end) {
                    s.loops[from_utf8(name)] = loop;
                }
            }
        }
    }

    // Typos ("suffle") would otherwise be silently ignored.
    static constexpr const char* kKnown[] = {"folder", "interval", "shuffle", "mute", "volume",
                                             "lock_screen_picture", "start_with_windows",
                                             "videos"};
    for (const auto& [key, value] : root.items()) {
        if (std::find(std::begin(kKnown), std::end(kKnown), key) == std::end(kKnown)) {
            result.warnings.push_back("unknown setting \"" + key + "\": ignored");
        }
    }
    return result;
}

std::string settings_to_json(const Settings& s) {
    // ordered_json keeps the keys in the order written here, so the file reads top to bottom.
    nlohmann::ordered_json root;
    root["folder"] = to_utf8(s.folder.wstring());
    root["interval"] = format_time(s.interval);
    root["shuffle"] = s.shuffle;
    root["mute"] = s.mute;
    root["volume"] = s.volume;
    root["lock_screen_picture"] = s.lock_screen_picture;
    root["start_with_windows"] = s.start_with_windows;
    root["videos"] = nlohmann::ordered_json::object();
    for (const auto& [file, loop] : s.loops) {
        nlohmann::ordered_json entry = nlohmann::ordered_json::object();
        if (loop.start) {
            entry["start"] = format_time(*loop.start);
        }
        if (loop.end) {
            entry["end"] = format_time(*loop.end);
        }
        root["videos"][to_utf8(file)] = entry;
    }
    return root.dump(2) + "\n";
}

std::string format_time(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0) {
        seconds = 0;
    }
    // Work in whole milliseconds so 0.1 + 0.2 style rounding never shows up as 0.30000001.
    const auto total_ms = static_cast<long long>(std::llround(seconds * 1000));
    const long long ms = total_ms % 1000;
    const long long total_s = total_ms / 1000;
    const long long h = total_s / 3600;
    const long long m = (total_s / 60) % 60;
    const long long sec = total_s % 60;

    std::string text = h > 0 ? std::format("{}:{:02}:{:02}", h, m, sec)
                             : std::format("{}:{:02}", m, sec);
    if (ms != 0) {
        std::string fraction = std::format(".{:03}", ms);
        while (fraction.back() == '0') {
            fraction.pop_back();  // 2.500 -> 2.5
        }
        text += fraction;
    }
    return text;
}

}  // namespace wallflow
