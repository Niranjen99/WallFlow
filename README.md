# WallFlow

[![CI](https://github.com/Niranjen99/WallFlow/actions/workflows/ci.yml/badge.svg)](https://github.com/Niranjen99/WallFlow/actions/workflows/ci.yml)

A lightweight live wallpaper app for Windows: it plays videos behind the desktop icons, rotates through a folder of videos with seamless switches, and pauses whenever a maximized or full-screen window (such as a game) covers the screen.

**Status: work in progress.** WallFlow plays one video in a loop behind the desktop icons, decoded on the GPU, lives in the system tray, and recovers by itself when Explorer restarts. Pausing for games and folder rotation come next. See the [Roadmap](#roadmap).

**Tech stack:** C++20, Win32 API, libmpv (FFmpeg, Direct3D 11), MSVC, CMake, GoogleTest, GitHub Actions.

## Build and run

Needs Visual Studio 2022 or later with the **Desktop development with C++** workload and the **C++ CMake tools for Windows** component.

**In Visual Studio:** File > Open > Folder, pick the `WallFlow` folder, choose the `Debug` preset, then Build > Build All.

**From a command prompt:** open the *x64 Native Tools Command Prompt* (Start menu, under Visual Studio), then:

```bat
cd path\to\WallFlow
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
build\debug\wallflow.exe "C:\path	oideo.mp4"
```

The first configure downloads libmpv (about 32 MB) and checks its SHA-256. For now the video is given on the command line; without one, your normal wallpaper stays. WallFlow runs in the system tray. Right-click its icon and choose **Quit WallFlow** to stop it and get your normal wallpaper back.

Presets: `debug`, `release`, and `asan` (Debug with AddressSanitizer, which CI also runs).

## Layout

```
src/
  main.cpp         the program's entry point
  wallflow/        everything else, built as a library so tests can link it
    desktop_layout   where the wallpaper window must go (pure logic, unit tested)
    win/             Win32 code: desktop attachment, wallpaper window, tray icon, app
tests/             GoogleTest unit tests
.github/workflows/ CI: MSVC Debug and AddressSanitizer builds on Windows
```

## How it gets behind the desktop icons

Windows has no official API for this. WallFlow sends Explorer the undocumented message `0x052C`, which makes it create a wallpaper layer behind the icons, then makes its own window a child of the right desktop window:

- **Windows 11 24H2 and later:** a child of `Progman`, placed directly below the icon view (`SHELLDLL_DefView`). `Progman` has no off-screen drawing surface on these versions (`WS_EX_NOREDIRECTIONBITMAP`), so the window is created as a layered window, which gets its own surface.
- **Older versions:** a child of the `WorkerW` window that sits behind the one holding the icons.

The choice is made by a pure function over a snapshot of the window tree, so both layouts are unit tested.

## Measured so far

Windows 11 build 26300, NVIDIA RTX 3070, 1920x1080 monitor, release build (2026-11-27):

| | Value |
|---|---|
| No video loaded | 42 KB executable, 1.5 MB private memory, 0 ms CPU over 30 s |
| Playing a 4K H.264 video | 0.4% of total CPU (12 logical cores), GPU video decoder 14.9%, GPU 3D 2.1% |
| Memory while playing 4K | 177 MB working set, about 345 MB private; mpv with nothing loaded: 19 MB |
| Same video decoded on the CPU instead | 61.5% of one core and 681 MB private: why hardware decoding is on |

Almost all of the memory is the decoder's pool of 4K frames (about 12 MB each). A 1080p video needs about a quarter of that.

## Troubleshooting

Set `WALLFLOW_MPV_LOG` to a file path before starting WallFlow and mpv writes its detailed log there (which decoder was used, whether hardware decoding worked, errors).

## Roadmap

- [x] Project skeleton: CMake presets, unit tests, CI
- [x] A window behind the desktop icons, tray icon, survives an Explorer restart
- [x] Play one video with hardware decoding (libmpv)
- [ ] Pause when a maximized or full-screen window covers the screen, and while locked
- [ ] Two-player engine: loop between a start and end point with no visible gap
- [ ] Rotate through a folder of videos with seamless switches
- [ ] Config file, audio settings, start with Windows
- [ ] Measured results: memory, CPU and GPU use, switch gap

## License

WallFlow is licensed under the [GNU General Public License v3.0](LICENSE). It uses [libmpv](https://mpv.io/), whose Windows builds include FFmpeg compiled with GPL components, so the combined program is GPLv3.
