cmake_minimum_required(VERSION 3.25)

project(WallFlow VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR "WallFlow is a Windows application and is built with MSVC.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(WALLFLOW_WERROR "Treat compiler warnings as errors" ON)
option(WALLFLOW_SANITIZE "Build with AddressSanitizer" OFF)
option(WALLFLOW_BUILD_TESTS "Build the unit tests" ON)

if(WALLFLOW_SANITIZE)
  # Applied to every target, GoogleTest included: MSVC refuses to link code built with and
  # without ASan when they pass standard containers to each other.
  add_compile_options(/fsanitize=address /Zi)
  # Debug builds turn on /RTC1 (run-time checks), which cannot be combined with ASan.
  string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
endif()

# Settings shared by every target in this project (but not by GoogleTest).
add_library(wallflow_options INTERFACE)
target_compile_options(wallflow_options INTERFACE
  /W4            # high warning level
  /permissive-   # standard-conforming C++ only
  /utf-8         # source files and string literals are UTF-8
  /Zc:__cplusplus
)
target_compile_definitions(wallflow_options INTERFACE
  UNICODE _UNICODE      # Windows API: use the wide-character (UTF-16) functions
  WIN32_LEAN_AND_MEAN   # leave rarely used parts out of <windows.h>
  NOMINMAX              # stop <windows.h> from defining min() and max() macros
)
if(WALLFLOW_WERROR)
  target_compile_options(wallflow_options INTERFACE /WX)
endif()

include(cmake/libmpv.cmake)

# Everything except the entry point lives in this library, so the tests can link against it.
add_library(wallflow_core
  src/wallflow/version.cpp
  src/wallflow/desktop_layout.cpp
  src/wallflow/coverage.cpp
  src/wallflow/loop_range.cpp
  src/wallflow/command_line.cpp
  src/wallflow/win/log.cpp
  src/wallflow/win/window_events.cpp
  src/wallflow/win/desktop.cpp
  src/wallflow/win/wallpaper_window.cpp
  src/wallflow/win/tray_icon.cpp
  src/wallflow/win/app.cpp
  src/wallflow/win/mpv_player.cpp
  src/wallflow/win/video_engine.cpp
)
target_include_directories(wallflow_core PUBLIC src)
target_compile_definitions(wallflow_core PRIVATE WALLFLOW_VERSION="${PROJECT_VERSION}")
# dwmapi: DwmGetWindowAttribute (cloaked windows). wtsapi32: lock/unlock notifications.
target_link_libraries(wallflow_core PUBLIC wallflow_options libmpv dwmapi wtsapi32)

# WIN32: a GUI program (entry point wWinMain, no console window).
add_executable(wallflow WIN32 src/main.cpp)
target_link_libraries(wallflow PRIVATE wallflow_core)
wallflow_copy_libmpv(wallflow)

# Loop-point benchmark (console program, needs a real video file to run).
add_executable(loop_bench bench/loop_bench.cpp)
target_link_libraries(loop_bench PRIVATE wallflow_core)
wallflow_copy_libmpv(loop_bench)

if(WALLFLOW_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
