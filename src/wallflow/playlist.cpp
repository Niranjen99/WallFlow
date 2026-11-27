#include "wallflow/playlist.hpp"

#include <algorithm>
#include <array>
#include <cwctype>
#include <string>
#include <string_view>
#include <system_error>

namespace wallflow {

namespace {

std::wstring lower(std::wstring text) {
    for (wchar_t& c : text) {
        c = static_cast<wchar_t>(std::towlower(c));
    }
    return text;
}

bool name_less(const std::filesystem::path& a, const std::filesystem::path& b) {
    const std::wstring an = a.filename().wstring();
    const std::wstring bn = b.filename().wstring();
    const std::wstring al = lower(an);
    const std::wstring bl = lower(bn);
    if (al != bl) {
        return al < bl;
    }
    return an < bn;
}

bool contains(const std::vector<std::filesystem::path>& list, const std::filesystem::path& p) {
    return std::find(list.begin(), list.end(), p) != list.end();
}

}  // namespace

bool is_video_file(const std::filesystem::path& file) {
    // Formats people actually use for wallpapers. mpv (FFmpeg) can play many more, but a folder
    // may also hold pictures, text files or thumbnails that must not be "played".
    static constexpr std::array<std::wstring_view, 13> kExtensions = {
        L".mp4", L".mkv", L".webm", L".mov", L".avi", L".wmv", L".m4v",
        L".flv", L".ts",  L".mpg",  L".mpeg", L".ogv", L".gif"};
    const std::wstring ext = lower(file.extension().wstring());
    return std::find(kExtensions.begin(), kExtensions.end(), ext) != kExtensions.end();
}

void sort_by_name(std::vector<std::filesystem::path>& files) {
    std::sort(files.begin(), files.end(), name_less);
}

std::vector<std::filesystem::path> scan_folder(const std::filesystem::path& folder) {
    std::vector<std::filesystem::path> videos;
    // The error_code overloads never throw: a folder that is missing, or deleted while being
    // read, just ends the scan.
    std::error_code error;
    for (std::filesystem::directory_iterator it(folder, error), end; !error && it != end;
         it.increment(error)) {
        std::error_code file_error;
        if (it->is_regular_file(file_error) && is_video_file(it->path())) {
            videos.push_back(it->path());
        }
    }
    sort_by_name(videos);
    return videos;
}

Playlist::Playlist(bool shuffle, std::uint32_t seed) : shuffle_(shuffle), random_(seed) {}

void Playlist::set_videos(std::vector<std::filesystem::path> videos) {
    sort_by_name(videos);
    if (shuffle_) {
        // Deleted videos leave the round; new ones join it at random places.
        std::erase_if(round_, [&](const auto& p) { return !contains(videos, p); });
        for (const auto& p : videos) {
            if (!contains(videos_, p)) {
                std::uniform_int_distribution<std::size_t> where(0, round_.size());
                round_.insert(round_.begin() + static_cast<std::ptrdiff_t>(where(random_)), p);
            }
        }
    }
    videos_ = std::move(videos);
    if (current_ && !contains(videos_, *current_)) {
        current_.reset();
    }
}

std::optional<std::filesystem::path> Playlist::next() {
    if (videos_.empty()) {
        current_.reset();
        return std::nullopt;
    }
    if (shuffle_) {
        if (round_.empty()) {
            start_round();
        }
        current_ = round_.front();
        round_.pop_front();
    } else {
        // The first video named after the last one played. That also works when the last one
        // has been deleted since: playback continues where it would have.
        auto it = videos_.begin();
        if (last_) {
            it = std::upper_bound(videos_.begin(), videos_.end(), *last_, name_less);
            if (it == videos_.end()) {
                it = videos_.begin();  // wrap around
            }
        }
        current_ = *it;
    }
    last_ = current_;
    return current_;
}

void Playlist::start_round() {
    round_.assign(videos_.begin(), videos_.end());
    std::shuffle(round_.begin(), round_.end(), random_);
    // Never the same video twice in a row across two rounds: swap it with a random other one.
    if (round_.size() > 1 && last_ && round_.front() == *last_) {
        std::uniform_int_distribution<std::size_t> other(1, round_.size() - 1);
        std::swap(round_.front(), round_[other(random_)]);
    }
}

}  // namespace wallflow
