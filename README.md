# WallFlow

[![CI](https://github.com/Niranjen99/WallFlow/actions/workflows/ci.yml/badge.svg)](https://github.com/Niranjen99/WallFlow/actions/workflows/ci.yml)

A lightweight live wallpaper app for Windows: it plays videos behind the desktop icons, rotates through a folder of videos with seamless switches, and pauses whenever a maximized or full-screen window (such as a game) covers the screen.

**Status: work in progress.** WallFlow plays one video behind the desktop icons, decoded on the GPU, and loops it between a start and end point without a black frame or a seek stall. It pauses while a maximized or full-screen window covers the screen, the PC is locked or the display is off, lives in the system tray, and recovers by itself when Explorer restarts. Folder rotation comes next. See the [Roadmap](#roadmap).

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
build\debug\wallflow.exe "C:\Videos\clip.mp4"
build\debug\wallflow.exe "C:\Videos\clip.mp4" --start 0:02 --end 0:05
```

The first configure downloads libmpv (about 32 MB) and checks its SHA-256. For now the video is given on the command line; without one, your normal wallpaper stays. `--start` and `--end` (seconds, `m:ss` or `h:mm:ss.s`) loop only that part of the video; without them the whole video loops. WallFlow runs in the system tray. Right-click its icon and choose **Quit WallFlow** to stop it and get your normal wallpaper back.

Presets: `debug`, `release`, and `asan` (Debug with AddressSanitizer, which CI also runs).

## Layout

```
src/
  main.cpp         the program's entry point
  wallflow/        everything else, built as a library so tests can link it
    desktop_layout   where the wallpaper window must go (pure logic, unit tested)
    coverage         the pause rule: is a real window covering the monitor? (pure logic, unit tested)
    loop_range       checks a loop start/end against the video, parses typed times (unit tested)
    command_line     the command-line arguments (unit tested)
    win/             Win32 code: desktop attachment, wallpaper window, tray icon, window
                     events, libmpv player, two-player video engine, app
tests/             GoogleTest unit tests (the player tests run headless, on CI too)
bench/             loop_bench: measures the pause at a loop point, two-player vs mpv's A-B loop
.github/workflows/ CI: MSVC Debug and AddressSanitizer builds on Windows
```

## How it gets behind the desktop icons

Windows has no official API for this. WallFlow sends Explorer the undocumented message `0x052C`, which makes it create a wallpaper layer behind the icons, then makes its own window a child of the right desktop window:

- **Windows 11 24H2 and later:** a child of `Progman`, placed directly below the icon view (`SHELLDLL_DefView`). `Progman` has no off-screen drawing surface on these versions (`WS_EX_NOREDIRECTIONBITMAP`), so the window is created as a layered window, which gets its own surface.
- **Older versions:** a child of the `WorkerW` window that sits behind the one holding the icons.

The choice is made by a pure function over a snapshot of the window tree, so both layouts are unit tested.

## How it knows when to pause

Nothing is polled. WallFlow subscribes to Windows' accessibility events (`SetWinEventHook`): focus changes, windows shown, hidden, minimized, cloaked, moved or resized. A burst of events (dragging a window sends dozens) is collapsed into one check 200 ms after it ends. The check takes a snapshot of the visible windows and asks a pure, unit-tested function whether one covers the monitor:

- **Covers:** a maximized window on that monitor, or a window as big as the whole monitor (full-screen games and videos).
- **Does not:** the desktop and taskbar, minimized windows, click-through overlays, and *cloaked* windows. Windows 11 keeps suspended apps and some system windows "visible" but undrawn; on the development machine a cloaked Settings app was maximized and a cloaked system window was exactly screen-sized, so without this filter the wallpaper would never play.

Lock/unlock (`WTSRegisterSessionNotification`) and display on/off (`RegisterPowerSettingNotification`) are pushed by Windows the same way.

## How it loops without a gap

The obvious way to loop part of a video is to seek back to the start at the end (mpv's built-in A-B loop does this). A seek has to decode from the keyframe before the start point, so the picture freezes for a moment at every loop.

WallFlow uses two players instead, each drawing into its own wallpaper window. The two windows are on top of each other, and only one is visible:

1. Player A plays. Player B has already loaded the same video, sought exactly to the loop start, and waits there, paused, in a fully transparent window.
2. At the loop end, A pauses on its last frame (`keep-open`, so the picture never goes black) and mpv reports `eof-reached` as an event.
3. WallFlow starts B, makes B's window opaque, then makes A's transparent. The screen goes from A's last frame straight to B's first.
4. A rewinds to the loop start, invisibly, and becomes the waiting player.

Two details made it reliable:

- **Which "frame ready" belongs to which seek?** mpv's `playback-restart` event doesn't say which command caused it, and one from an earlier seek can still be queued. Each load and seek gets its own reply id, and a restart only counts once mpv has replied to that command.
- **A bad loop range never breaks playback.** A start past the end of the file, or a loop shorter than 0.5 s, falls back to the whole video. That can only be checked once the file is loaded and its length is known.

Folder rotation will use the same swap, with the next video in the waiting player.

## Measured so far

Windows 11 build 26300, NVIDIA RTX 3070, 1920x1080 monitor, release build (2026-11-27):

| | Value |
|---|---|
| No video loaded | 42 KB executable, 1.5 MB private memory, 0 ms CPU over 30 s |
| Playing a 4K H.264 video | 0.4% of total CPU (12 logical cores), GPU video decoder 14.9%, GPU 3D 2.1% |
| Paused (4K video loaded, covered by a maximized window) | 31 ms of CPU in 30 s, GPU video decoder 0%, GPU 3D 0% |
| Memory while playing 4K | 177 MB working set, about 345 MB private; mpv with nothing loaded: 19 MB |
| Same video decoded on the CPU instead | 61.5% of one core and 681 MB private: why hardware decoding is on |

Almost all of the memory is the decoder's pool of 4K frames (about 12 MB each). A 1080p video needs about a quarter of that. The two-player engine (below) holds two such pools: **694 MB private** with two players for the same 4K video, against about 345 MB with one.

**Pause at a loop point**, measured with `bench/loop_bench.exe` (10 loops per video, 4K H.264, GPU decoding, no drawing, so the compositor is not included). Overrun = how much longer a loop took than its length in whole frames, i.e. how long the picture stood still too long:

| Video, loop | Two-player (WallFlow) | mpv A-B loop (one player, seek back) |
|---|---|---|
| 30 fps, 2.0 to 5.0 s | 0.2 ms mean, 0.3 ms max | 43.1 ms mean, 43.5 ms max |
| 30 fps, 1.1 to 4.6 s | 0.3 ms mean, 1.2 ms max | 57.0 ms mean, 58.2 ms max |
| 60 fps, 3.3 to 7.7 s | 13.7 ms mean, 14.5 ms max | 113.9 ms mean, 115.2 ms max |

At 30 fps the seek-back loop freezes the picture for one to two frames at every loop, while the two-player swap is under 2 ms. At 60 fps the two-player swap measured 8 to 14 ms on every loop range tried, still under one frame (16.7 ms); the cause of that delay is not identified yet.

```bat
build\release\loop_bench.exe "C:\Videos\clip.mp4" 2 5 10
```

## Troubleshooting

Set `WALLFLOW_LOG` to a file path before starting WallFlow and it logs what it does there, with timestamps (attached, paused and why, playing). Set `WALLFLOW_MPV_LOG` and mpv writes its own detailed log (which decoder was used, whether hardware decoding worked, errors).

## Roadmap

- [x] Project skeleton: CMake presets, unit tests, CI
- [x] A window behind the desktop icons, tray icon, survives an Explorer restart
- [x] Play one video with hardware decoding (libmpv)
- [x] Pause when a maximized or full-screen window covers the screen, while locked, and while the display is off
- [x] Two-player engine: loop between a start and end point with no visible gap
- [ ] Rotate through a folder of videos with seamless switches
- [ ] Config file, audio settings, start with Windows
- [ ] Measured results: memory, CPU and GPU use, switch gap

## License

WallFlow is licensed under the [GNU General Public License v3.0](LICENSE). It uses [libmpv](https://mpv.io/), whose Windows builds include FFmpeg compiled with GPL components, so the combined program is GPLv3.
