#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include "wallflow/win/settings_file.hpp"

namespace fs = std::filesystem;
using wallflow::win::read_text_file;
using wallflow::win::write_text_file;

TEST(SettingsFile, WritesReadsAndReplaces) {
    const fs::path dir = fs::temp_directory_path() / L"wallflow-settings-test-é";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path file = dir / L"settings.json";

    EXPECT_FALSE(read_text_file(file).has_value());  // missing

    ASSERT_TRUE(write_text_file(file, "{ \"mute\": true }\n"));
    EXPECT_EQ(read_text_file(file), "{ \"mute\": true }\n");

    // Replacing works even while another program has the file open for reading.
    {
        std::ifstream reader(file);
        ASSERT_TRUE(reader.is_open());
        ASSERT_TRUE(write_text_file(file, "{}"));
    }
    EXPECT_EQ(read_text_file(file), "{}");
    EXPECT_FALSE(fs::exists(dir / L"settings.json.tmp"));  // no leftover temporary file

    fs::remove_all(dir);
}

TEST(SettingsFile, FolderIsInAppData) {
    const fs::path folder = wallflow::win::settings_folder();
    EXPECT_EQ(folder.filename(), L"WallFlow");
    EXPECT_TRUE(fs::is_directory(folder));
}
