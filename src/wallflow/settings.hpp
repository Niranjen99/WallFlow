#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "wallflow/loop_range.hpp"

namespace wallflow {

// Everything the user can set. Stored as %APPDATA%\WallFlow\settings.json, for example:
//
//   {
//     "folder": "C:\\Users\\me\\Videos\\Wallpapers",
//     "interval": "5:00",
//     "shuffle": true,
//     "mute": true,
//     "volume": 50,
//     "lock_screen_picture": false,
//     "start_with_windows": false,
//     "videos": {
//       "goku-hero.mp4": { "start": "0:02", "end": "0:05" },
//       "glitchy.mp4": { "compatible_decoding": true }
//     }
//   }
//
// Every field is optional; a missing or invalid one keeps its default.
struct Settings {
    std::filesystem::path folder;    // videos to rotate through; empty = none chosen yet
    double interval = 300;           // seconds of playing time per video
    bool shuffle = false;
    bool mute = true;
    int volume = 50;                 // 0 to 100, used when not muted
    bool lock_screen_picture = false;
    bool start_with_windows = false;
    // Loop range per video, by file name (matched ignoring case).
    std::map<std::wstring, LoopSetting> loops;
    // Videos decoded with Vulkan instead of DirectX, by file name (matched ignoring case).
    // For videos the GPU's DirectX decoder shows with corrupted blocks.
    std::set<std::wstring> compatible_decoding;
    // Play only this video of the folder (file name), looping, with no rotation. Empty, or a
    // file that is not in the folder: rotate through the folder as usual.
    std::wstring only_video;

    // The loop setting for `video`, or none.
    [[nodiscard]] LoopSetting loop_for(const std::filesystem::path& video) const;
    [[nodiscard]] bool compatible_for(const std::filesystem::path& video) const;

    friend bool operator==(const Settings&, const Settings&) = default;
};

inline constexpr double kMinInterval = 10;  // seconds; shorter is almost certainly a typo

struct ParsedSettings {
    Settings settings;
    bool valid_json = true;             // false: the text was not JSON at all (e.g. mid-edit)
    std::vector<std::string> warnings;  // one per value that was ignored, for the log
};

// Reads settings from JSON text. Never throws. Comments (// and /* */) are allowed, so users
// can annotate the file.
ParsedSettings parse_settings(std::string_view json);

// Writes settings as indented JSON, times as "m:ss".
std::string settings_to_json(const Settings& settings);

// 90 -> "1:30", 3723.5 -> "1:02:03.5", 5 -> "0:05". parse_time() reads it back.
std::string format_time(double seconds);

}  // namespace wallflow
