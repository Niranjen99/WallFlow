include(FetchContent)

# GoogleTest is downloaded at configure time and used only by the tests.
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.17.0
  GIT_SHALLOW TRUE
)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
# Use the same C runtime DLL as our code (/MD). Without this GoogleTest links the static one
# and the linker reports a runtime library mismatch.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(wallflow_tests
  test_version.cpp
  test_desktop_layout.cpp
  test_mpv_player.cpp
)
target_link_libraries(wallflow_tests PRIVATE wallflow_core GTest::gtest_main)
wallflow_copy_libmpv(wallflow_tests)

include(GoogleTest)
# PRE_TEST: list the tests when ctest runs, not while building.
gtest_discover_tests(wallflow_tests DISCOVERY_MODE PRE_TEST)
