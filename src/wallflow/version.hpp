#pragma once

#include <string_view>

namespace wallflow {

// The app version, set from the project() line in CMakeLists.txt.
// Wide (UTF-16) like every string we hand to the Windows API.
std::wstring_view version() noexcept;

}  // namespace wallflow
