#include "wallflow/win/folder_watcher.hpp"

#include <stdexcept>

namespace wallflow::win {

FolderWatcher::FolderWatcher(const std::filesystem::path& folder, std::function<void()> on_change)
    : on_change_(std::move(on_change)) {
    // FILE_LIST_DIRECTORY: the right needed to watch a folder. FILE_FLAG_BACKUP_SEMANTICS is
    // required to open a folder at all. FILE_FLAG_OVERLAPPED: reads return at once and signal
    // an event later, so the thread can wait for "changed" and "stop" at the same time.
    // The share flags let everyone else keep adding, renaming and deleting files.
    HANDLE handle = ::CreateFileW(folder.c_str(), FILE_LIST_DIRECTORY,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                  OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
                                  nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("cannot watch the folder (missing or no access)");
    }
    folder_.reset(handle);
    // Manual-reset events (TRUE): they stay signalled until reset, so a signal cannot be lost.
    io_done_.reset(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
    stop_.reset(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!io_done_ || !stop_) {
        throw std::runtime_error("CreateEventW failed");
    }
    // The first read is made here, before the constructor returns: Windows only records
    // changes from the first ReadDirectoryChangesW call on, so a file added right after
    // construction would otherwise be missed if the thread had not started yet. From then on
    // it keeps recording between calls.
    if (!start_read()) {
        throw std::runtime_error("ReadDirectoryChangesW failed");
    }
    try {
        thread_ = std::thread(&FolderWatcher::run, this);
    } catch (...) {
        // No thread: the read just started must not outlive buffer_ (freed with this object).
        DWORD bytes = 0;
        ::CancelIoEx(folder_.get(), &overlapped_);
        ::GetOverlappedResult(folder_.get(), &overlapped_, &bytes, TRUE);
        throw;
    }
}

FolderWatcher::~FolderWatcher() {
    ::SetEvent(stop_.get());
    if (thread_.joinable()) {
        thread_.join();
    }
}

bool FolderWatcher::start_read() {
    constexpr DWORD kWhat = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE |
                            FILE_NOTIFY_CHANGE_SIZE;
    overlapped_ = OVERLAPPED{};
    overlapped_.hEvent = io_done_.get();
    ::ResetEvent(io_done_.get());
    return ::ReadDirectoryChangesW(folder_.get(), buffer_.data(),
                                   static_cast<DWORD>(buffer_.size()), FALSE /* not subfolders */,
                                   kWhat, nullptr, &overlapped_, nullptr) != FALSE;
}

void FolderWatcher::run() {
    for (;;) {
        // A read is always pending here.
        const HANDLE events[] = {io_done_.get(), stop_.get()};
        const DWORD which = ::WaitForMultipleObjects(2, events, FALSE, INFINITE);

        DWORD bytes = 0;
        if (which != WAIT_OBJECT_0) {
            // Stopping (or the wait failed). Cancel the pending read and wait until Windows
            // has really finished with buffer_ and overlapped_.
            ::CancelIoEx(folder_.get(), &overlapped_);
            ::GetOverlappedResult(folder_.get(), &overlapped_, &bytes, TRUE);
            return;
        }
        if (!::GetOverlappedResult(folder_.get(), &overlapped_, &bytes, FALSE)) {
            return;  // the folder handle broke (folder deleted, drive removed)
        }
        // bytes == 0 means more changes than fit in the buffer: still just "changed".
        on_change_();
        if (!start_read()) {
            return;
        }
    }
}

}  // namespace wallflow::win
