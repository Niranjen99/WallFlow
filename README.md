# WallFlow

[![CI](https://github.com/Niranjen99/WallFlow/actions/workflows/ci.yml/badge.svg)](https://github.com/Niranjen99/WallFlow/actions/workflows/ci.yml)

A lightweight live wallpaper app for Windows: it plays videos behind the desktop icons, rotates through a folder of videos with seamless switches, and pauses whenever a maximized or full-screen window (such as a game) covers the screen.

**Status: work in progress.** Only the project skeleton exists so far (build, tests, CI). See the [Roadmap](#roadmap).

**Tech stack:** C++20, Win32 API, MSVC, CMake, GoogleTest, GitHub Actions.

## Build and run

Needs Visual Studio 2022 or later with the **Desktop development with C++** workload and the **C++ CMake tools for Windows** component.

**In Visual Studio:** File > Open > Folder, pick the `WallFlow` folder, choose the `Debug` preset, then Build > Build All.

**From a command prompt:** open the *x64 Native Tools Command Prompt* (Start menu, under Visual Studio), then:

```bat
cd path\to\WallFlow
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
build\debug\wallflow.exe
```

Presets: `debug`, `release`, and `asan` (Debug with AddressSanitizer, which CI also runs).

## Layout

```
src/
  main.cpp         the program's entry point
  wallflow/        everything else, built as a library so tests can link it
tests/             GoogleTest unit tests
.github/workflows/ CI: MSVC Debug and AddressSanitizer builds on Windows
```

## Roadmap

- [x] Project skeleton: CMake presets, unit tests, CI
- [ ] A window behind the desktop icons, tray icon, survives an Explorer restart
- [ ] Play one video with hardware decoding (libmpv)
- [ ] Pause when a maximized or full-screen window covers the screen, and while locked
- [ ] Two-player engine: loop between a start and end point with no visible gap
- [ ] Rotate through a folder of videos with seamless switches
- [ ] Config file, audio settings, start with Windows
- [ ] Measured results: memory, CPU and GPU use, switch gap
