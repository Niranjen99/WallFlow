#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace wallflow::win {

// %APPDATA%\WallFlow (the user's roaming app data), created if missing. Throws
// std::runtime_error if Windows cannot tell where that is.
std::filesystem::path settings_folder();

// The whole file as bytes, or nothing if it does not exist or cannot be read. Other programs
// may have it open (an editor saving it) while it is read.
std::optional<std::string> read_text_file(const std::filesystem::path& file);

// Replaces `file` with `text` in one step: the text goes to a temporary file next to it, which
// is then renamed over the old one. Anyone reading the file (WallFlow's own reload, an editor)
// sees either the old or the new version, never half of it. If another program keeps the
// file open so it cannot be replaced, it is overwritten in place instead (not atomic, but the
// save is not lost). Returns false on failure.
bool write_text_file(const std::filesystem::path& file, std::string_view text);

}  // namespace wallflow::win
