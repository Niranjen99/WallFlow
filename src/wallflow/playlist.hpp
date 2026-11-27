#pragma once

#include <cstdint>
#include <deque>
#include <filesystem>
#include <optional>
#include <random>
#include <vector>

namespace wallflow {

// True if the file name has a video extension WallFlow plays (case does not matter).
bool is_video_file(const std::filesystem::path& file);

// The video files directly inside `folder` (not in subfolders), sorted by file name, ignoring
// case. A missing or unreadable folder gives an empty list.
std::vector<std::filesystem::path> scan_folder(const std::filesystem::path& folder);

// Sorts by file name ignoring case: "a.mp4", "B.mp4", "c.mp4" (plain sorting would put "B"
// first). Ties are broken case-sensitively so the order is always the same.
void sort_by_name(std::vector<std::filesystem::path>& files);

// The order in which videos are played.
//
// In order: by file name, starting over after the last one.
// Shuffled: every video plays once per round, in random order; a new round never starts with
// the video that just played (unless there is only one).
//
// The list can change at any time (files added or deleted in the folder); set_videos() keeps
// the current position as well as it can.
class Playlist {
public:
    // `seed` makes the shuffle repeatable in tests; the app passes a random one.
    Playlist(bool shuffle, std::uint32_t seed);

    // Replaces the list. Videos that were deleted are never returned again; new ones are
    // added to what is still to come.
    void set_videos(std::vector<std::filesystem::path> videos);

    // Moves to the next video and returns it; nothing if the list is empty. With one video,
    // that video again.
    std::optional<std::filesystem::path> next();

    // The video next() returned last, if it is still in the list.
    [[nodiscard]] const std::optional<std::filesystem::path>& current() const noexcept {
        return current_;
    }

    [[nodiscard]] std::size_t size() const noexcept { return videos_.size(); }

private:
    void start_round();

    bool shuffle_;
    std::mt19937 random_;
    std::vector<std::filesystem::path> videos_;  // sorted by name
    std::optional<std::filesystem::path> current_;
    // The last video played, kept even after it was deleted: in order, to find the one after
    // it; shuffled, so a new round does not start with it.
    std::optional<std::filesystem::path> last_;
    std::deque<std::filesystem::path> round_;  // shuffled: still to play in this round
};

}  // namespace wallflow
