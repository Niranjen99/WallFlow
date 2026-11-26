#include <gtest/gtest.h>

#include "wallflow/version.hpp"

TEST(Version, MatchesTheProjectVersionInCMake) {
    EXPECT_EQ(wallflow::version(), L"0.1.0");
}
