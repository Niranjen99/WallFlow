#pragma once

#include <windows.h>

#include <memory>

#include "wallflow/command_line.hpp"
#include "wallflow/playlist.hpp"
#include "wallflow/settings.hpp"
#include "wallflow/win/folder_watcher.hpp"
#include "wallflow/win/settings_window.hpp"
#include "wallflow/win/tray_icon.hpp"
#include "wallflow/win/video_engine.hpp"
#include "wallflow/win/window_events.hpp"
#include "wallflow/win/window_proc.hpp"

namespace wallflow::win {

// The whole app: a hidden controller window that receives tray clicks and system
// notifications, the tray icon, and the video engine (two wallpaper windows and players).
//
// The controller is a normal (hidden) top-level window, not a message-only window, because
// Windows sends the "TaskbarCreated" broadcast only to top-level windows.
class App {
public:
    // What plays comes from settings.json, unless the command line names a video or folder
    // (then the command line wins for what plays; the file still sets everything else).
    App(HINSTANCE instance, CommandLine start);
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // Runs the message loop until the user quits. Returns the exit code.
    int run();

private:
    template <typename T>
    friend LRESULT CALLBACK window_proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT on_message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    // (Re)creates the wallpaper windows and attaches them; retries on a timer if the desktop
    // is not ready yet (e.g. right after Explorer restarts).
    void start_wallpaper();
    void try_attach();
    void quit();
    void start_player();
    void rescan_folder();

    // Where the videos come from: the command line if it names a video or folder, else the
    // settings file.
    [[nodiscard]] bool command_line_decides() const;
    [[nodiscard]] std::filesystem::path folder() const;
    [[nodiscard]] std::optional<VideoSource> source_for(const std::filesystem::path& video) const;
    // (Re)builds the playlist and the folder watcher for folder(). Returns the first video.
    std::optional<VideoSource> open_folder();
    VideoEngine::NextVideo next_video_function();

    // settings.json: read it (creating it with defaults the first time) and apply changes.
    void load_settings();
    void apply_settings(const Settings& changed);
    void open_settings_window();
    // A change made in the settings window: save it to settings.json and apply it.
    void change_settings(const Settings& changed);
    // Preview from the settings window: show `video` on the desktop now, looping, with no
    // rotation, until end_preview() (Apply, or the window closes) brings back what was playing.
    void preview(const VideoSource& video);
    void end_preview();
    // The settings window closed: end a preview, and go back to the applied audio (the window
    // plays mute/volume changes at once, before they are applied).
    void settings_window_closed();
    // Settings that apply whatever plays: audio, start with Windows.
    void apply_general_settings();

    // Pause rule: re-checks whether a window covers the monitor, then applies all reasons.
    void check_coverage();
    void update_pause();

    HINSTANCE instance_;
    HWND hwnd_ = nullptr;
    UINT taskbar_created_ = 0;  // message number Windows assigns to "TaskbarCreated"
    CommandLine start_;
    std::unique_ptr<TrayIcon> tray_;
    std::unique_ptr<VideoEngine> engine_;
    bool engine_playing_ = false;  // play() was called on the current engine
    std::unique_ptr<Playlist> playlist_;  // with --folder
    std::unique_ptr<FolderWatcher> folder_watcher_;
    std::filesystem::path watched_folder_;  // the folder folder_watcher_ watches
    Settings settings_;
    std::filesystem::path settings_file_;
    std::unique_ptr<FolderWatcher> settings_watcher_;
    std::unique_ptr<SettingsWindow> settings_window_;  // created the first time it is opened
    int attach_attempts_ = 0;
    bool quitting_ = false;
    bool previewing_ = false;  // the controller window is gone: leave the message loop

    std::unique_ptr<WindowEvents> events_;
    HPOWERNOTIFY display_notify_ = nullptr;
    // Reasons to pause. Playback runs only when all are false.
    bool covered_ = false;       // a maximized or full-screen window covers the monitor
    bool locked_ = false;        // the session is locked (lock screen)
    bool display_off_ = false;   // the monitor is off (power saving)
    bool paused_ = false;        // what was last sent to the engine
};

}  // namespace wallflow::win
