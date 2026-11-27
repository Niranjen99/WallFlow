#include "wallflow/win/autostart.hpp"

#include <windows.h>

#include <vector>

namespace wallflow::win {

namespace {

constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

}  // namespace

std::optional<std::wstring> autostart_command(const std::wstring& name) {
    // RegGetValueW reads a value without opening the key by hand. First call: ask for the size
    // in bytes; second call: read. RRF_RT_REG_SZ accepts only text values.
    DWORD bytes = 0;
    if (::RegGetValueW(HKEY_CURRENT_USER, kRunKey, name.c_str(), RRF_RT_REG_SZ, nullptr, nullptr,
                       &bytes) != ERROR_SUCCESS) {
        return std::nullopt;
    }
    std::vector<wchar_t> text(bytes / sizeof(wchar_t) + 1, L'\0');
    bytes = static_cast<DWORD>(text.size() * sizeof(wchar_t));
    if (::RegGetValueW(HKEY_CURRENT_USER, kRunKey, name.c_str(), RRF_RT_REG_SZ, nullptr,
                       text.data(), &bytes) != ERROR_SUCCESS) {
        return std::nullopt;
    }
    return std::wstring(text.data());  // stops at the terminating zero
}

bool set_autostart(const std::wstring& name, const std::wstring& command, bool enable) {
    if (!enable) {
        const LSTATUS status = ::RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, name.c_str());
        return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;  // gone either way
    }
    // REG_SZ data is the text including its terminating zero, measured in bytes.
    const auto bytes = static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t));
    return ::RegSetKeyValueW(HKEY_CURRENT_USER, kRunKey, name.c_str(), REG_SZ, command.c_str(),
                             bytes) == ERROR_SUCCESS;
}

std::wstring own_command_line() {
    // MAX_PATH (260) is not a real limit any more; grow the buffer until the path fits.
    std::vector<wchar_t> path(MAX_PATH);
    for (;;) {
        const DWORD length = ::GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length < path.size()) {
            return L"\"" + std::wstring(path.data(), length) + L"\"";
        }
        path.resize(path.size() * 2);
    }
}

}  // namespace wallflow::win
