#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>

#include <gtest/gtest.h>

#include "wallflow/win/folder_watcher.hpp"

namespace fs = std::filesystem;
using namespace std::chrono_literals;
using wallflow::win::FolderWatcher;

namespace {

// Counts on_change calls (made on the watcher's thread) and lets the test wait for one.
class Changes {
public:
    std::function<void()> callback() {
        return [this] {
            {
                std::lock_guard lock(mutex_);
                ++count_;
            }
            cv_.notify_all();
        };
    }
    bool wait_for_more_than(int seen, std::chrono::milliseconds timeout = 5s) {
        std::unique_lock lock(mutex_);
        return cv_.wait_for(lock, timeout, [&] { return count_ > seen; });
    }
    int count() {
        std::lock_guard lock(mutex_);
        return count_;
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    int count_ = 0;
};

class FolderWatcherTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = fs::temp_directory_path() / L"wallflow-watch-test-é";
        fs::remove_all(dir_);
        fs::create_directories(dir_);
    }
    void TearDown() override { fs::remove_all(dir_); }
    fs::path dir_;
};

}  // namespace

TEST_F(FolderWatcherTest, ReportsAddedRenamedAndDeletedFiles) {
    Changes changes;
    FolderWatcher watcher(dir_, changes.callback());

    std::ofstream(dir_ / L"new.mp4").put('x');
    ASSERT_TRUE(changes.wait_for_more_than(0)) << "adding a file was not reported";

    int seen = changes.count();
    fs::rename(dir_ / L"new.mp4", dir_ / L"renamed.mp4");
    ASSERT_TRUE(changes.wait_for_more_than(seen)) << "renaming was not reported";

    seen = changes.count();
    fs::remove(dir_ / L"renamed.mp4");
    ASSERT_TRUE(changes.wait_for_more_than(seen)) << "deleting was not reported";
}

TEST_F(FolderWatcherTest, StopsCleanlyWithNothingHappening) {
    // The destructor must cancel the pending read and join the thread, not hang.
    const auto start = std::chrono::steady_clock::now();
    {
        Changes changes;
        FolderWatcher watcher(dir_, changes.callback());
    }
    EXPECT_LT(std::chrono::steady_clock::now() - start, 2s);
}

TEST_F(FolderWatcherTest, MissingFolderThrows) {
    Changes changes;
    EXPECT_THROW(FolderWatcher(dir_ / L"missing", changes.callback()), std::runtime_error);
}
