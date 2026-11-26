#include "wallflow/win/log.hpp"

#include <share.h>
#include <windows.h>

#include <array>
#include <cstdio>
#include <cwchar>
#include <memory>

namespace wallflow::win {

namespace {

struct CloseFile {
    void operator()(std::FILE* file) const noexcept { std::fclose(file); }
};

// Opened on first use. A function-local static is initialised exactly once, even if two
// threads call this at the same time (guaranteed since C++11).
std::FILE* log_file() {
    static const std::unique_ptr<std::FILE, CloseFile> file = [] {
        std::array<wchar_t, MAX_PATH> path{};
        if (::GetEnvironmentVariableW(L"WALLFLOW_LOG", path.data(), MAX_PATH) == 0) {
            return std::unique_ptr<std::FILE, CloseFile>{};
        }
        // _wfsopen with _SH_DENYWR: others may read the log while WallFlow writes it.
        return std::unique_ptr<std::FILE, CloseFile>(::_wfsopen(path.data(), L"ab", _SH_DENYWR));
    }();
    return file.get();
}

std::string to_utf8(std::wstring_view wide) {
    if (wide.empty()) {
        return {};
    }
    const int size = static_cast<int>(wide.size());
    const int length = ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), size, nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<std::size_t>(length), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), size, utf8.data(), length, nullptr, nullptr);
    return utf8;
}

}  // namespace

void log(std::wstring_view text) {
    const std::wstring line = L"WallFlow: " + std::wstring(text) + L"\n";
    ::OutputDebugStringW(line.c_str());

    if (std::FILE* file = log_file()) {
        SYSTEMTIME now{};
        ::GetLocalTime(&now);
        std::array<char, 32> stamp{};
        std::snprintf(stamp.data(), stamp.size(), "%02u:%02u:%02u.%03u ", now.wHour, now.wMinute,
                      now.wSecond, now.wMilliseconds);
        const std::string utf8 = std::string(stamp.data()) + to_utf8(text) + "\n";
        std::fwrite(utf8.data(), 1, utf8.size(), file);
        std::fflush(file);  // so the file is complete even if WallFlow is killed
    }
}

std::wstring to_wide(std::string_view utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int size = static_cast<int>(utf8.size());
    const int length = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), size, nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), size, wide.data(), length);
    return wide;
}

}  // namespace wallflow::win
