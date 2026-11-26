#pragma once

#include <string>
#include <string_view>

namespace wallflow::win {

// Writes one line to the debugger output (Visual Studio's Output window, DebugView) and, if
// the environment variable WALLFLOW_LOG names a file, appends it there with a timestamp.
void log(std::wstring_view text);

// UTF-8 (what mpv and C++ exceptions use) to UTF-16 (what Windows uses).
std::wstring to_wide(std::string_view utf8);

}  // namespace wallflow::win
