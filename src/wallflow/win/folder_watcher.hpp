#pragma once

#include <windows.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <thread>

namespace wallflow::win {

// Tells you when files in a folder are added, deleted, renamed or written (not subfolders).
// It does not say what changed: the caller simply rescans the folder, which is cheap and also
// covers the case where Windows had too many changes to report them one by one.
//
// Threading: a background thread waits for changes and calls `on_change` there, so on_change
// must do nothing but hand over to the UI thread (PostMessage). The destructor stops the
// thread and waits for it.
class FolderWatcher {
public:
    // Throws std::runtime_error if the folder cannot be opened.
    FolderWatcher(const std::filesystem::path& folder, std::function<void()> on_change);
    ~FolderWatcher();

    FolderWatcher(const FolderWatcher&) = delete;
    FolderWatcher& operator=(const FolderWatcher&) = delete;

private:
    // Closes a kernel handle (folder handle, event) when the owner goes away.
    struct CloseHandleDeleter {
        void operator()(HANDLE handle) const noexcept { ::CloseHandle(handle); }
    };
    // HANDLE is void*, so unique_ptr<void, ...> owns it.
    using UniqueHandle = std::unique_ptr<void, CloseHandleDeleter>;

    // Asks Windows for the next batch of changes; returns at once (overlapped).
    bool start_read();
    void run();

    UniqueHandle folder_;
    UniqueHandle io_done_;  // signalled by Windows when ReadDirectoryChangesW has news
    UniqueHandle stop_;     // signalled by the destructor
    std::function<void()> on_change_;
    // Windows writes change records into buffer_ and completes overlapped_ while a read is
    // pending, so both must outlive every read: they are members, not locals.
    // DWORD-aligned, as ReadDirectoryChangesW requires. The records are never parsed.
    alignas(DWORD) std::array<std::byte, 16 * 1024> buffer_{};
    OVERLAPPED overlapped_{};
    std::thread thread_;    // declared last: started after everything it uses exists
};

}  // namespace wallflow::win
