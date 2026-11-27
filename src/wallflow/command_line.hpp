#pragma once

#include <filesystem>
#include <span>
#include <string>

#include "wallflow/loop_range.hpp"

namespace wallflow {

// What WallFlow was started with, until the config file (Step 7) takes over:
//   wallflow.exe "C:\Videos\clip.mp4" [--start 0:05] [--end 0:20]
struct CommandLine {
    std::filesystem::path video;  // empty: show a solid colour
    LoopSetting loop;
    std::wstring error;           // not empty if the arguments were wrong
};

// `args` are the arguments after the program name.
CommandLine parse_command_line(std::span<const std::wstring> args);

}  // namespace wallflow
