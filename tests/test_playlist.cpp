#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <vector>

#include <gtest/gtest.h>

#include "wallflow/playlist.hpp"

namespace fs = std::filesystem;
using wallflow::Playlist;

namespace {

std::vector<fs::path> paths(std::initializer_list<const wchar_t*> names) {
    std::vector<fs::path> result;
    for (const wchar_t* name : names) {
        result.emplace_back(fs::path(L"C:/Wallpapers") / name);
    }
    return result;
}

std::vector<fs::path> take(Playlist& playlist, int count) {
    std::vector<fs::path> result;
    for (int i = 0; i < count; ++i) {
        result.push_back(playlist.next().value());
    }
    return result;
}

}  // namespace

TEST(Playlist, RecognisesVideoFiles) {
    EXPECT_TRUE(wallflow::is_video_file(L"clip.mp4"));
    EXPECT_TRUE(wallflow::is_video_file(L"C:/x/Clip.MKV"));
    EXPECT_TRUE(wallflow::is_video_file(L"a.b.webm"));
    EXPECT_FALSE(wallflow::is_video_file(L"cover.jpg"));
    EXPECT_FALSE(wallflow::is_video_file(L"notes.txt"));
    EXPECT_FALSE(wallflow::is_video_file(L"mp4"));  // no extension at all
}

TEST(Playlist, SortsByNameIgnoringCase) {
    auto files = paths({L"c.mp4", L"B.mp4", L"a.mp4"});
    wallflow::sort_by_name(files);
    EXPECT_EQ(files, paths({L"a.mp4", L"B.mp4", L"c.mp4"}));
}

TEST(Playlist, EmptyListGivesNothing) {
    Playlist playlist(false, 1);
    EXPECT_FALSE(playlist.next().has_value());
    playlist.set_videos({});
    EXPECT_FALSE(playlist.next().has_value());
}

TEST(Playlist, InOrderWrapsAround) {
    Playlist playlist(false, 1);
    playlist.set_videos(paths({L"c.mp4", L"a.mp4", L"b.mp4"}));
    EXPECT_EQ(take(playlist, 4), paths({L"a.mp4", L"b.mp4", L"c.mp4", L"a.mp4"}));
}

TEST(Playlist, OneVideoRepeats) {
    for (const bool shuffle : {false, true}) {
        Playlist playlist(shuffle, 1);
        playlist.set_videos(paths({L"only.mp4"}));
        EXPECT_EQ(take(playlist, 3), paths({L"only.mp4", L"only.mp4", L"only.mp4"}));
    }
}

TEST(Playlist, InOrderContinuesAfterTheCurrentVideoIsDeleted) {
    Playlist playlist(false, 1);
    playlist.set_videos(paths({L"a.mp4", L"b.mp4", L"c.mp4"}));
    EXPECT_EQ(playlist.next(), paths({L"a.mp4"})[0]);
    EXPECT_EQ(playlist.next(), paths({L"b.mp4"})[0]);

    playlist.set_videos(paths({L"a.mp4", L"c.mp4"}));  // b.mp4 deleted while playing
    EXPECT_FALSE(playlist.current().has_value());
    EXPECT_EQ(playlist.next(), paths({L"c.mp4"})[0]);  // what came after b
}

TEST(Playlist, InOrderPlaysANewFileInItsPlace) {
    Playlist playlist(false, 1);
    playlist.set_videos(paths({L"a.mp4", L"c.mp4"}));
    EXPECT_EQ(playlist.next(), paths({L"a.mp4"})[0]);
    playlist.set_videos(paths({L"a.mp4", L"b.mp4", L"c.mp4"}));  // b.mp4 added
    EXPECT_EQ(take(playlist, 2), paths({L"b.mp4", L"c.mp4"}));
}

TEST(Playlist, ShufflePlaysEachVideoOncePerRound) {
    const auto videos = paths({L"1.mp4", L"2.mp4", L"3.mp4", L"4.mp4", L"5.mp4"});
    Playlist playlist(true, 42);
    playlist.set_videos(videos);
    for (int round = 0; round < 20; ++round) {
        auto played = take(playlist, 5);
        std::sort(played.begin(), played.end());
        EXPECT_EQ(played, videos) << "round " << round;
    }
}

TEST(Playlist, ShuffleNeverRepeatsBackToBack) {
    // Over many seeds and rounds, including the joins between rounds. Two videos is the
    // hardest case: every round must start with the one that did not just play.
    for (const auto& videos : {paths({L"a.mp4", L"b.mp4"}), paths({L"a.mp4", L"b.mp4", L"c.mp4"})}) {
        for (std::uint32_t seed = 0; seed < 50; ++seed) {
            Playlist playlist(true, seed);
            playlist.set_videos(videos);
            const auto played = take(playlist, 30);
            for (std::size_t i = 1; i < played.size(); ++i) {
                ASSERT_NE(played[i], played[i - 1]) << "seed " << seed << " position " << i;
            }
        }
    }
}

TEST(Playlist, ShuffleIsRepeatableWithTheSameSeed) {
    const auto videos = paths({L"1.mp4", L"2.mp4", L"3.mp4", L"4.mp4", L"5.mp4", L"6.mp4"});
    Playlist a(true, 7);
    Playlist b(true, 7);
    a.set_videos(videos);
    b.set_videos(videos);
    EXPECT_EQ(take(a, 12), take(b, 12));
}

TEST(Playlist, ShuffleDropsDeletedAndAddsNewVideos) {
    Playlist playlist(true, 3);
    playlist.set_videos(paths({L"a.mp4", L"b.mp4", L"c.mp4", L"d.mp4"}));
    const fs::path first = playlist.next().value();

    // Delete everything except the one playing, add two new ones.
    playlist.set_videos({first, paths({L"x.mp4"})[0], paths({L"y.mp4"})[0]});
    std::set<fs::path> rest_of_round;
    rest_of_round.insert(playlist.next().value());
    rest_of_round.insert(playlist.next().value());
    EXPECT_EQ(rest_of_round, (std::set<fs::path>{paths({L"x.mp4"})[0], paths({L"y.mp4"})[0]}));
}

TEST(Playlist, ScansOnlyVideosInTheFolder) {
    const fs::path dir = fs::temp_directory_path() / L"wallflow-scan-test-é";
    fs::remove_all(dir);
    fs::create_directories(dir / L"sub");
    for (const wchar_t* name : {L"b.mp4", L"A.mkv", L"cover.jpg", L"sub/deep.mp4"}) {
        std::ofstream(dir / name).put('x');
    }
    fs::create_directories(dir / L"folder.mp4");  // a folder with a video-like name

    const auto found = wallflow::scan_folder(dir);
    EXPECT_EQ(found, (std::vector<fs::path>{dir / L"A.mkv", dir / L"b.mp4"}));
    fs::remove_all(dir);

    EXPECT_TRUE(wallflow::scan_folder(dir).empty());  // missing folder: empty, no exception
}
