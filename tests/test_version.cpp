#include <gtest/gtest.h>

#include "wallflow/version.hpp"

// WALLFLOW_EXPECTED_VERSION comes from project(VERSION ...) in CMakeLists.txt (see
// tests/CMakeLists.txt), so this checks the wiring without being edited at every release.
TEST(Version, MatchesTheProjectVersionInCMake) {
    EXPECT_EQ(wallflow::version(), L"" WALLFLOW_EXPECTED_VERSION);
}
