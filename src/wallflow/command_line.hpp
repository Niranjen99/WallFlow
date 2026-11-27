#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>

#include "wallflow/loop_range.hpp"

namespace wallflow {

// What WallFlow was started with, until the config file (Step 7) takes over:
//   wallflow.exe "C:\Videos\clip.mp4" [--start 0:05] [--end 0:20]
//   wallflow.exe --folder "C:\Videos" [--interval 5:00] [--shuffle]
struct CommandLine {
    std::filesystem::path video;   // one video to loop
    LoopSetting loop;              // which part of it
    std::filesystem::path folder;  // or: rotate through the videos in this folder
    double interval = 300;         // seconds of playing time per video (folder only)
    bool shuffle = false;          // folder only
    std::wstring error;            // not empty if the arguments were wrong
};

// `args` are the arguments after the program name. Nothing at all is valid (no video: the
// normal wallpaper stays).
CommandLine parse_command_line(std::span<const std::wstring> args);

}  // namespace wallflow
