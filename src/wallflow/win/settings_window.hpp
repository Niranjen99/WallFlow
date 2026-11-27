#pragma once

#include <windows.h>
#include <commctrl.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "wallflow/settings.hpp"
#include "wallflow/settings_form.hpp"
#include "wallflow/win/window_proc.hpp"

struct IFileDialog;  // <shobjidl.h>, only used through a pointer here

namespace wallflow::win {

// Frees a GDI object (brush, font, pen) when the unique_ptr goes away.
struct GdiDeleter {
    void operator()(void* object) const { ::DeleteObject(static_cast<HGDIOBJ>(object)); }
};
// HBRUSH is a pointer type (HBRUSH__*); unique_ptr wants the type it points to.
template <typename Handle>
using UniqueGdi = std::unique_ptr<std::remove_pointer_t<Handle>, GdiDeleter>;

// The settings window, opened from the tray. Plain Win32 controls, laid out in code, scaled
// for the monitor's DPI, and coloured for the Windows light or dark app theme (switching live).
//
// Changes wait for the Apply button (or Enter). The window keeps two copies of the settings:
// `applied_` (what WallFlow uses now) and `settings_` (what the window shows, with the user's
// pending edits). Apply checks the text fields, then hands `settings_` to `on_change` (the app
// saves settings.json and applies it). A mistake is shown in red and nothing is applied.
class SettingsWindow {
public:
    using ChangeHandler = std::function<void(const Settings&)>;
    using Action = std::function<void()>;
    using PreviewHandler =
        std::function<void(const std::filesystem::path& file, const LoopSetting& loop, bool compatible)>;
    using AudioHandler = std::function<void(bool muted, int volume)>;

    // on_preview: show a video on the desktop now (not applied); on_audio: mute/volume moved
    // (heard at once, not applied); on_closed: the window closed (end a preview, undo
    // audio that was not applied).
    SettingsWindow(HINSTANCE instance, ChangeHandler on_change,
                   PreviewHandler on_preview, AudioHandler on_audio, Action on_closed);
    ~SettingsWindow();

    SettingsWindow(const SettingsWindow&) = delete;
    SettingsWindow& operator=(const SettingsWindow&) = delete;

    // Opens the window (or brings it to the front) showing `settings`.
    void show(const Settings& settings);
    // The settings changed elsewhere (settings.json edited by hand): show the new values.
    void update(const Settings& settings);
    // Videos were added to or deleted from the folder: refresh the list.
    void videos_changed();
    // Tab, Shift+Tab, Enter and Esc between the controls. The message loop calls this first;
    // true means the message was handled here.
    bool handle_dialog_message(MSG& msg);
    // Closes the folder picker if it is open (the app is quitting).
    void cancel_folder_picker();

private:
    template <typename T>
    friend LRESULT CALLBACK window_proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT on_message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    static LRESULT CALLBACK list_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam,
                                      UINT_PTR id, DWORD_PTR self);
    static LRESULT CALLBACK slider_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam,
                                        UINT_PTR id, DWORD_PTR self);
    // The slider moved (any way): show the number, repaint, play it at once.
    void volume_moved();

    struct Palette {
        COLORREF background, text, secondary, field, track, accent, knob, error;
    };

    void create_window();
    void create_controls();
    void create_fonts();
    void layout();
    void apply_theme();
    void fill_from_settings();
    void fill_video_list();
    void show_selected_loop();
    void on_command(int id, int code);
    bool commit_interval();
    bool commit_loop();
    bool apply();
    void close_asking();
    void update_apply_button();
    void clear_loop();
    void preview();
    // Greys out the interval and shuffle while one video plays alone (they do nothing then).
    void update_rotation_controls();
    void browse_folder();
    void toggle(int id);
    void changed(const std::wstring& status);
    void set_status(const std::wstring& text, bool error);
    void draw_toggle(const DRAWITEMSTRUCT& item);
    LRESULT draw_slider(const NMCUSTOMDRAW& draw);
    [[nodiscard]] bool toggle_on(int id) const;
    [[nodiscard]] int selected_row() const;
    [[nodiscard]] int scale(int value) const;
    [[nodiscard]] std::wstring text_of(HWND control) const;

    HINSTANCE instance_;
    ChangeHandler on_change_;
    PreviewHandler on_preview_;
    AudioHandler on_audio_;
    Action on_closed_;
    Settings settings_;  // shown in the window, with edits not applied yet
    Settings applied_;   // what WallFlow uses now
    std::vector<VideoRow> rows_;

    HWND hwnd_ = nullptr;
    IFileDialog* picker_ = nullptr;  // while the folder picker is open; owned by browse_folder()
    UINT dpi_ = 96;
    bool dark_ = false;
    Palette palette_{};
    bool status_error_ = false;
    ULONG_PTR gdiplus_ = 0;  // GDI+ session (anti-aliased switches), see the constructor
    HICON big_icon_ = nullptr;
    HICON small_icon_ = nullptr;
    UniqueGdi<HFONT> font_;
    UniqueGdi<HFONT> heading_font_;
    UniqueGdi<HBRUSH> background_brush_;
    UniqueGdi<HBRUSH> field_brush_;

    // Child controls (owned by the window: destroyed with it).
    HWND folder_heading_ = nullptr, folder_ = nullptr, browse_ = nullptr;
    HWND interval_label_ = nullptr, interval_ = nullptr, interval_hint_ = nullptr;
    HWND shuffle_ = nullptr, mute_ = nullptr, volume_label_ = nullptr, volume_ = nullptr;
    HWND volume_value_ = nullptr, autostart_ = nullptr;
    HWND videos_heading_ = nullptr, list_ = nullptr;
    HWND loop_start_label_ = nullptr, loop_start_ = nullptr, loop_end_label_ = nullptr;
    HWND loop_end_ = nullptr, whole_video_ = nullptr, loop_hint_ = nullptr;
    HWND compatible_ = nullptr, preview_ = nullptr, only_ = nullptr;
    HWND status_ = nullptr, apply_ = nullptr, close_ = nullptr;
    HWND version_ = nullptr;
    HWND tooltip_ = nullptr;
};

}  // namespace wallflow::win
