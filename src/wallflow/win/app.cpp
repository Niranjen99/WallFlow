#include "wallflow/win/app.hpp"

#include <wtsapi32.h>

#include <cstring>
#include <random>
#include <utility>
#include <stdexcept>
#include <string>

#include "wallflow/coverage.hpp"
#include "wallflow/win/autostart.hpp"
#include "wallflow/win/log.hpp"
#include "wallflow/win/settings_file.hpp"
#include "wallflow/win/theme.hpp"

namespace wallflow::win {

namespace {

constexpr wchar_t kClassName[] = L"WallFlow.Controller";
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kPlayerMessage = WM_APP + 2;  // mpv has events waiting
constexpr UINT kFolderMessage = WM_APP + 3;  // something changed in the video folder
constexpr UINT kSettingsMessage = WM_APP + 4;  // something changed in %APPDATA%\WallFlow
constexpr UINT_PTR kAttachTimer = 1;
constexpr UINT kAttachRetryMs = 500;
// 60 seconds: when WallFlow starts with Windows, Explorer may still be busy logging in.
constexpr int kMaxAttachAttempts = 120;
constexpr UINT_PTR kCoverageTimer = 2;
constexpr UINT_PTR kPreloadTimer = 3;  // the engine's "load the next video now"
constexpr UINT_PTR kFolderTimer = 4;
// Copying a video into the folder sends many change events. Rescan once, this long after the
// last one.
constexpr UINT kFolderDelayMs = 1000;
constexpr UINT_PTR kSettingsTimer = 5;
// Editors may write a file in several steps; read it once they are done.
constexpr UINT kSettingsDelayMs = 300;
// Window events come in bursts (dragging a window sends dozens). The check runs once, this
// long after the last event of a burst.
constexpr UINT kCoverageDelayMs = 200;

Rect primary_monitor() {
    MONITORINFO info{};
    info.cbSize = sizeof info;
    ::GetMonitorInfoW(::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &info);
    return {info.rcMonitor.left, info.rcMonitor.top, info.rcMonitor.right, info.rcMonitor.bottom};
}

}  // namespace

App::App(HINSTANCE instance, CommandLine start) : instance_(instance), start_(std::move(start)) {
    // Explorer broadcasts this message to all top-level windows whenever the taskbar is
    // (re)created, which is how apps learn that Explorer restarted.
    taskbar_created_ = ::RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = window_proc<App>;
    wc.hInstance = instance;
    wc.lpszClassName = kClassName;
    if (::RegisterClassExW(&wc) == 0) {
        throw std::runtime_error("RegisterClassExW failed for the controller window");
    }
    // Never shown. WS_EX_TOOLWINDOW keeps it out of the taskbar and Alt+Tab regardless.
    ::CreateWindowExW(WS_EX_TOOLWINDOW, kClassName, L"WallFlow", WS_OVERLAPPED, 0, 0, 0, 0,
                      nullptr, nullptr, instance, this);
    if (hwnd_ == nullptr) {
        throw std::runtime_error("CreateWindowExW failed for the controller window");
    }

    follow_system_theme_for_menus();  // before the first menu is shown
    tray_ = std::make_unique<TrayIcon>(hwnd_, kTrayMessage);
    tray_->show();

    // Lock and unlock arrive as WM_WTSSESSION_CHANGE; display on/off as WM_POWERBROADCAST.
    // Both are notifications Windows pushes to us: nothing is polled.
    ::WTSRegisterSessionNotification(hwnd_, NOTIFY_FOR_THIS_SESSION);
    display_notify_ = ::RegisterPowerSettingNotification(hwnd_, &GUID_CONSOLE_DISPLAY_STATE,
                                                         DEVICE_NOTIFY_WINDOW_HANDLE);
    events_ = std::make_unique<WindowEvents>(hwnd_, kCoverageTimer, kCoverageDelayMs);

    // Settings first: they decide what plays. A failure here (no AppData?) is not fatal: the
    // defaults apply and nothing is saved.
    try {
        settings_file_ = settings_folder() / L"settings.json";
        load_settings();
        HWND controller = hwnd_;
        settings_watcher_ = std::make_unique<FolderWatcher>(
            settings_file_.parent_path(),
            [controller] { ::PostMessageW(controller, kSettingsMessage, 0, 0); });
    } catch (const std::exception& e) {
        log(L"settings file not available: " + to_wide(e.what()));
    }

    check_coverage();  // something may already be maximized when WallFlow starts
    start_wallpaper();
}

App::~App() {
    // The controller window is a raw HWND, so it is destroyed by hand, after everything that
    // still uses it.
    settings_window_.reset();
    engine_.reset();
    tray_.reset();
    if (hwnd_ != nullptr) {
        ::DestroyWindow(hwnd_);
    }
}

int App::run() {
    MSG msg{};
    // GetMessage waits without using CPU until a message arrives. It returns 0 for WM_QUIT
    // and -1 on error.
    // quitting_ as well as WM_QUIT: if Quit happens while another message loop runs (the folder
    // picker's), that loop can swallow WM_QUIT, and this one would then wait forever.
    while (!quitting_ && ::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        // Keyboard navigation in the settings window (Tab, Enter, Esc) happens here.
        if (settings_window_ && settings_window_->handle_dialog_message(msg)) {
            continue;
        }
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

void App::start_wallpaper() {
    ::KillTimer(hwnd_, kAttachTimer);
    // After an Explorer restart the old windows are usually already gone.
    engine_.reset();
    engine_ = std::make_unique<VideoEngine>(instance_, hwnd_, kPlayerMessage, kPreloadTimer);
    engine_->set_audio(settings_.mute, settings_.volume);
    engine_playing_ = false;
    attach_attempts_ = 0;
    try_attach();
}

void App::try_attach() {
    ++attach_attempts_;
    if (const auto layout = engine_->attach()) {
        ::KillTimer(hwnd_, kAttachTimer);
        log(std::wstring(L"attached behind the icons (") +
            (*layout == DesktopLayout::Modern ? L"modern" : L"classic") + L" layout), attempt " +
            std::to_wstring(attach_attempts_));
        start_player();
        return;
    }
    if (attach_attempts_ >= kMaxAttachAttempts) {
        ::KillTimer(hwnd_, kAttachTimer);
        log(L"desktop not found, giving up");
        ::MessageBoxW(nullptr, L"WallFlow could not find the desktop to draw on.", L"WallFlow",
                      MB_OK | MB_ICONWARNING);
        return;
    }
    // SetTimer with the same id replaces the existing timer, so calling this again is safe.
    ::SetTimer(hwnd_, kAttachTimer, kAttachRetryMs, nullptr);
}

bool App::command_line_decides() const {
    return !start_.video.empty() || !start_.folder.empty();
}

std::filesystem::path App::folder() const {
    return command_line_decides() ? start_.folder : settings_.folder;
}

std::optional<std::filesystem::path> App::only_video() const {
    if (!start_.video.empty() || settings_.only_video.empty() || folder().empty()) {
        return std::nullopt;
    }
    const std::filesystem::path file = folder() / settings_.only_video;
    std::error_code error;  // e.g. the folder is gone: treat as "not there", do not throw
    if (!std::filesystem::is_regular_file(file, error)) {
        return std::nullopt;
    }
    return file;
}

void App::apply_rotation() {
    if (!engine_ || !start_.video.empty()) {
        return;  // a single video from the command line never rotates
    }
    only_active_ = only_video().has_value();
    if (only_active_) {
        engine_->set_rotation(std::nullopt, {});  // loop the one video forever
    } else {
        const double interval = command_line_decides() ? start_.interval : settings_.interval;
        engine_->set_rotation(interval, next_video_function());
    }
}

std::optional<VideoSource> App::source_for(const std::filesystem::path& video) const {
    if (!start_.video.empty()) {
        return VideoSource{video, start_.loop, settings_.compatible_for(video)};
    }
    return VideoSource{video, settings_.loop_for(video), settings_.compatible_for(video)};
}

std::optional<VideoSource> App::open_folder() {
    const std::filesystem::path dir = folder();
    const bool shuffle = command_line_decides() ? start_.shuffle : settings_.shuffle;
    // A different order every run: seed the shuffle from the OS's random source.
    playlist_ = std::make_unique<Playlist>(shuffle, std::random_device{}());
    if (watched_folder_ != dir) {
        folder_watcher_.reset();
        watched_folder_.clear();
        if (!dir.empty()) {
            try {
                // Runs on the watcher's thread: only post. The HWND is copied, not `this`.
                HWND controller = hwnd_;
                folder_watcher_ = std::make_unique<FolderWatcher>(
                    dir, [controller] { ::PostMessageW(controller, kFolderMessage, 0, 0); });
                watched_folder_ = dir;
            } catch (const std::exception& e) {
                log(L"not watching the folder: " + to_wide(e.what()));
            }
        }
    }
    if (dir.empty()) {
        log(L"no video folder set: choose one in the settings (click the tray icon)");
        return std::nullopt;
    }
    playlist_->set_videos(scan_folder(dir));
    log(L"folder " + dir.wstring() + L": " + std::to_wstring(playlist_->size()) + L" videos");
    if (const auto only = only_video()) {
        log(L"playing only " + only->filename().wstring());
        return source_for(*only);
    }
    if (const auto video = playlist_->next()) {
        return source_for(*video);
    }
    return std::nullopt;
}

VideoEngine::NextVideo App::next_video_function() {
    // The engine calls this when it is time for the next video. `this` outlives the engine
    // (App owns it), it is only called on the UI thread, and it reads playlist_ at call time,
    // so it keeps working when the playlist is rebuilt.
    return [this]() -> std::optional<VideoSource> {
        if (playlist_) {
            if (const auto video = playlist_->next()) {
                return source_for(*video);
            }
        }
        return std::nullopt;
    };
}

void App::start_player() {
    if (engine_playing_) {
        return;
    }
    std::optional<VideoSource> first;
    VideoEngine::NextVideo next;
    if (!start_.video.empty()) {
        first = source_for(start_.video);
    } else {
        first = open_folder();
        only_active_ = only_video().has_value();
        if (!only_active_) {
            next = next_video_function();  // empty `next` = no rotation: loop the one video
        }
    }
    if (!first) {
        return;  // nothing to play: the normal wallpaper stays
    }
    const double interval = command_line_decides() ? start_.interval : settings_.interval;
    try {
        // If it should be paused right now (e.g. Explorer restarted while a game runs), it
        // loads paused: the first frame is shown, nothing more is decoded.
        paused_ = covered_ || locked_ || display_off_;
        engine_->play(*first, paused_, interval, std::move(next));
        engine_playing_ = true;
    } catch (const std::exception& e) {
        // Keep the engine (its windows stay invisible): an Explorer restart tries again.
        log(L"player failed: " + to_wide(e.what()));
    }
}

void App::rescan_folder() {
    if (settings_window_) {
        settings_window_->videos_changed();
    }
    if (!playlist_ || folder().empty()) {
        return;
    }
    const std::size_t before = playlist_->size();
    playlist_->set_videos(scan_folder(folder()));
    log(L"folder changed: " + std::to_wstring(before) + L" -> " +
        std::to_wstring(playlist_->size()) + L" videos");
    // The folder was empty and now has videos: start. (Otherwise the engine picks the new
    // list up by itself at the next switch.)
    if (!engine_playing_ && engine_ && engine_->alive()) {
        start_player();
        return;
    }
    // The "only" video was added to or deleted from the folder: play it, or rotate again.
    if (engine_playing_ && !previewing_ && only_video().has_value() != only_active_) {
        apply_rotation();
        if (const auto only = only_video()) {
            engine_->switch_now(*source_for(*only));
        }
    }
}

void App::load_settings() {
    const std::optional<std::string> text = read_text_file(settings_file_);
    if (!text) {
        // First run (or the file was deleted): write the defaults so there is something to edit.
        log(L"creating " + settings_file_.wstring());
        if (!write_text_file(settings_file_, settings_to_json(settings_))) {
            log(L"could not write the settings file");
        }
        apply_general_settings();
        return;
    }
    const ParsedSettings parsed = parse_settings(*text);
    for (const std::string& warning : parsed.warnings) {
        log(L"settings.json: " + to_wide(warning));
    }
    if (!parsed.valid_json) {
        // Probably saved halfway through an edit: keep what we have until it is valid again.
        return;
    }
    apply_settings(parsed.settings);
}

void App::apply_settings(const Settings& changed) {
    // std::exchange: settings_ becomes `changed`, and `old` gets what settings_ was before.
    const Settings old = std::exchange(settings_, changed);
    // Also on an unchanged file: at start this puts the exe's current path in the Run key.
    apply_general_settings();
    if (old == changed) {
        return;  // e.g. the file was saved without changes
    }
    log(L"settings changed");
    if (settings_window_) {
        settings_window_->update(settings_);  // e.g. settings.json edited by hand
    }
    if (command_line_decides() || !engine_) {
        return;  // the command line decides what plays
    }

    if (old.folder != changed.folder || old.shuffle != changed.shuffle ||
        old.only_video != changed.only_video) {
        if (!engine_playing_) {
            start_player();
        } else if (const auto first = open_folder()) {
            apply_rotation();
            engine_->switch_now(*first);
        }
        return;
    }
    if (engine_playing_ && old.interval != changed.interval) {
        apply_rotation();
    }
    if (engine_playing_ &&
        (old.loops != changed.loops || old.compatible_decoding != changed.compatible_decoding)) {
        // A new loop range or decoder for the video on screen applies right away (seamlessly).
        if (const auto now_playing = engine_->current()) {
            const auto wanted = source_for(now_playing->file);
            if (wanted && *wanted != *now_playing) {
                engine_->switch_now(*wanted);
            }
        }
    }
}

void App::apply_general_settings() {
    if (engine_) {
        engine_->set_audio(settings_.mute, settings_.volume);
    }
    // Registered with this exe's path every time, so moving WallFlow keeps autostart working.
    const std::wstring command = own_command_line();
    const auto registered = autostart_command(L"WallFlow");
    const bool want = settings_.start_with_windows;
    if (want != registered.has_value() || (want && *registered != command)) {
        if (set_autostart(L"WallFlow", command, want)) {
            log(want ? L"start with Windows: on" : L"start with Windows: off");
        } else {
            log(L"could not change start with Windows");
        }
    }
}

void App::open_settings_window() {
    if (!settings_window_) {
        settings_window_ = std::make_unique<SettingsWindow>(
            instance_, [this](const Settings& changed) { change_settings(changed); },
            [this](const std::filesystem::path& file, const LoopSetting& loop, bool compatible) {
                preview({file, loop, compatible});
            },
            [this](bool muted, int volume) {
                if (engine_) {
                    engine_->set_audio(muted, volume);
                }
            },
            [this] { settings_window_closed(); });
    }
    settings_window_->show(settings_);
}

void App::preview(const VideoSource& video) {
    if (!engine_ || !engine_->alive()) {
        return;
    }
    log(L"preview: " + video.file.filename().wstring());
    if (!engine_playing_) {
        // Nothing was playing (no folder yet): play the preview alone. end_preview() rebuilds
        // the wallpaper, which then plays whatever the settings say (maybe nothing).
        engine_->play(video, covered_ || locked_ || display_off_, std::nullopt, {});
        engine_playing_ = true;
        previewing_ = true;
        return;
    }
    previewing_ = true;
    engine_->set_rotation(std::nullopt, {});  // stay on the preview: no switching
    engine_->switch_now(video);
}

void App::settings_window_closed() {
    end_preview();
    if (engine_) {
        engine_->set_audio(settings_.mute, settings_.volume);  // what is applied
    }
}

void App::end_preview() {
    if (!previewing_) {
        return;
    }
    previewing_ = false;
    log(L"preview ended");
    if (!engine_) {
        return;
    }
    // What should play now: the current video of the folder (the preview did not move the
    // playlist on), or the command line's video.
    std::optional<VideoSource> back;
    if (!start_.video.empty()) {
        back = source_for(start_.video);
    } else if (const auto only = only_video()) {
        back = source_for(*only);
    } else if (playlist_ && playlist_->current()) {
        back = source_for(*playlist_->current());
    }
    if (!back) {
        start_wallpaper();  // the preview was all that played
        return;
    }
    apply_rotation();
    if (engine_->current() != back) {
        engine_->switch_now(*back);
    }
}

void App::change_settings(const Settings& changed) {
    end_preview();  // Apply: back to the normal wallpaper, with the new settings
    // Saved first, then applied. The folder watcher then reports our own write; reading it
    // back finds nothing new, so nothing happens twice.
    if (!settings_file_.empty() && !write_text_file(settings_file_, settings_to_json(changed))) {
        log(L"could not save the settings file");
    }
    apply_settings(changed);
}

void App::check_coverage() {
    const bool covered = is_monitor_covered(primary_monitor(), snapshot_windows());
    if (covered != covered_) {
        covered_ = covered;
        log(covered ? L"monitor covered by a window" : L"monitor uncovered");
        update_pause();
    }
}

void App::update_pause() {
    const bool pause = covered_ || locked_ || display_off_;
    if (engine_playing_ && pause != paused_) {
        engine_->set_paused(pause);
        paused_ = pause;
        log(pause ? L"paused" : L"playing");
    }
}

void App::quit() {
    // Remove the wallpaper and the tray icon while the controller window still exists (the
    // tray icon is identified by its owner window), then end the message loop.
    ::KillTimer(hwnd_, kAttachTimer);
    ::KillTimer(hwnd_, kFolderTimer);
    ::KillTimer(hwnd_, kSettingsTimer);
    if (settings_window_) {
        // Not destroyed here: we may be running inside its folder picker right now (Quit
        // chosen from the tray while the picker is open). Only the picker is told to close.
        settings_window_->cancel_folder_picker();
    }
    folder_watcher_.reset();  // stops its thread
    settings_watcher_.reset();
    events_.reset();
    if (display_notify_ != nullptr) {
        ::UnregisterPowerSettingNotification(display_notify_);
        display_notify_ = nullptr;
    }
    ::WTSUnRegisterSessionNotification(hwnd_);
    engine_.reset();
    tray_.reset();
    ::DestroyWindow(hwnd_);  // WM_DESTROY posts WM_QUIT
}

LRESULT App::on_message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    // RegisterWindowMessage numbers are only known at run time, so this cannot be a case label.
    if (msg == taskbar_created_ && taskbar_created_ != 0) {
        log(L"Explorer restarted: re-adding the tray icon and re-attaching");
        tray_->show();
        start_wallpaper();
        return 0;
    }

    switch (msg) {
        case WM_NCCREATE:
            hwnd_ = hwnd;
            break;

        case kTrayMessage:
            if (LOWORD(lparam) == WM_LBUTTONUP) {
                open_settings_window();
            } else if (LOWORD(lparam) == WM_RBUTTONUP) {
                switch (tray_->show_menu()) {
                    case TrayIcon::kOpenSettings:
                        open_settings_window();
                        break;
                    case TrayIcon::kQuit:
                        quit();
                        break;
                    default:
                        break;
                }
            }
            return 0;

        case kPlayerMessage:
            if (engine_) {
                engine_->on_player_message();
            }
            return 0;

        case kSettingsMessage:
            ::SetTimer(hwnd, kSettingsTimer, kSettingsDelayMs, nullptr);
            return 0;

        case kFolderMessage:
            // Restarts the countdown: the rescan runs once a burst of changes is over.
            ::SetTimer(hwnd, kFolderTimer, kFolderDelayMs, nullptr);
            return 0;

        case WM_TIMER:
            if (wparam == kAttachTimer) {
                try_attach();
            } else if (wparam == kSettingsTimer) {
                ::KillTimer(hwnd, kSettingsTimer);
                load_settings();
            } else if (wparam == kFolderTimer) {
                ::KillTimer(hwnd, kFolderTimer);
                rescan_folder();
            } else if (wparam == kPreloadTimer) {
                if (engine_) {
                    engine_->on_preload_timer();
                }
            } else if (wparam == kCoverageTimer) {
                ::KillTimer(hwnd, kCoverageTimer);  // timers repeat; this one should fire once
                check_coverage();
            }
            return 0;

        case WM_WTSSESSION_CHANGE:
            if (wparam == WTS_SESSION_LOCK || wparam == WTS_SESSION_UNLOCK) {
                locked_ = wparam == WTS_SESSION_LOCK;
                log(locked_ ? L"session locked" : L"session unlocked");
                update_pause();
            }
            return 0;

        case WM_POWERBROADCAST:
            if (wparam == PBT_POWERSETTINGCHANGE) {
                // lparam points at a block Windows owns, valid only during this message.
                const auto* setting = reinterpret_cast<const POWERBROADCAST_SETTING*>(lparam);
                if (setting->PowerSetting == GUID_CONSOLE_DISPLAY_STATE &&
                    setting->DataLength == sizeof(DWORD)) {
                    // Data is a DWORD: 0 = off, 1 = on, 2 = dimmed (still visible).
                    DWORD state = 0;
                    std::memcpy(&state, setting->Data, sizeof state);
                    display_off_ = state == 0;
                    log(display_off_ ? L"display off" : L"display on");
                    update_pause();
                }
                return TRUE;
            }
            break;

        case WM_SETTINGCHANGE:
            if (is_theme_change(lparam)) {
                log(apps_use_dark_theme() ? L"theme: dark" : L"theme: light");
                refresh_menu_theme();
            }
            break;

        case WM_DISPLAYCHANGE:
            // Resolution or monitor layout changed: fit the primary monitor again.
            if (engine_ && engine_->alive()) {
                engine_->attach();
            }
            return 0;

        case WM_CLOSE:
            // Someone asked us to close (Windows shutting down, an installer, a script).
            // Clean up exactly like the tray menu's Quit.
            quit();
            return 0;

        case WM_DESTROY:
            quitting_ = true;
            ::PostQuitMessage(0);
            return 0;

        case WM_NCDESTROY:
            hwnd_ = nullptr;
            break;

        default:
            break;
    }
    return ::DefWindowProcW(hwnd, msg, wparam, lparam);
}

}  // namespace wallflow::win
