#include "wallflow/win/settings_file.hpp"

#include <windows.h>
#include <shlobj.h>  // SHGetKnownFolderPath

#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <system_error>

namespace wallflow::win {

std::filesystem::path settings_folder() {
    // The shell allocates the returned string; CoTaskMemFree releases it, here via unique_ptr.
    PWSTR raw = nullptr;
    const HRESULT hr = ::SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE, nullptr, &raw);
    std::unique_ptr<wchar_t, decltype(&::CoTaskMemFree)> owned(raw, &::CoTaskMemFree);
    if (FAILED(hr)) {
        throw std::runtime_error("cannot find the AppData folder");
    }
    std::filesystem::path folder = std::filesystem::path(owned.get()) / L"WallFlow";
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    return folder;
}

std::optional<std::string> read_text_file(const std::filesystem::path& file) {
    // MSVC's ifstream opens files shared for reading and writing, so an editor that has the
    // file open does not block this.
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool write_text_file(const std::filesystem::path& file, std::string_view text) {
    std::filesystem::path temp = file;
    temp += L".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out.write(text.data(), static_cast<std::streamsize>(text.size()))) {
            return false;
        }
    }  // closed (and flushed) here, before the rename
    // MOVEFILE_REPLACE_EXISTING: rename over the old file in one step.
    // MOVEFILE_WRITE_THROUGH: return only once the rename is on disk.
    // The rename fails while another program has the file open without allowing deletion
    // (FILE_SHARE_DELETE): a virus scanner, or any C++ ifstream. Such reads are short, so try
    // a few times.
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (::MoveFileExW(temp.c_str(), file.c_str(),
                          MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            return true;
        }
        ::Sleep(50);
    }
    // Still held open: overwrite in place instead. A reader could then see half a file, which
    // the settings reload treats as "not valid JSON, keep the current settings"; losing the
    // user's save would be worse.
    std::error_code error;
    std::filesystem::remove(temp, error);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    return static_cast<bool>(out.write(text.data(), static_cast<std::streamsize>(text.size())));
}

}  // namespace wallflow::win
