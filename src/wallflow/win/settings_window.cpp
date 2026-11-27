#include "wallflow/win/settings_window.hpp"

#include <shobjidl.h>  // IFileOpenDialog
#include <uxtheme.h>   // SetWindowTheme
#include <windowsx.h>
#include <wrl/client.h>  // Microsoft::WRL::ComPtr

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <optional>
#include <string>

// GDI+ uses unqualified min/max, which NOMINMAX removes from <windows.h>: give it std's.
namespace Gdiplus {
using std::max;
using std::min;
}  // namespace Gdiplus
#include <gdiplus.h>

#include "wallflow/playlist.hpp"
#include "wallflow/version.hpp"
#include "wallflow/win/theme.hpp"

namespace wallflow::win {

namespace {

constexpr wchar_t kClassName[] = L"WallFlow.Settings";
// Sent to ourselves after a loop field loses focus: by then the focus has moved, so we can
// tell "moved to the other loop field" (do not apply half a loop yet) from "left the loop".
constexpr UINT kLoopFocusMessage = WM_APP + 1;

Gdiplus::Color to_gdiplus(COLORREF color) {
    return Gdiplus::Color(255, GetRValue(color), GetGValue(color), GetBValue(color));
}

// GDI draws curves without anti-aliasing (stair-stepped edges); GDI+ smooths them.
// PixelOffsetModeHalf puts pixel centres at x.5, so shapes on whole coordinates stay sharp.
void smooth(Gdiplus::Graphics& g) {
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
}

enum ControlId : int {
    kFolder = 100,
    kBrowse,
    kInterval,
    kShuffle,
    kMute,
    kVolume,
    kStartWithWindows,
    kVideoList,
    kLoopStart,
    kLoopEnd,
    kWholeVideo,
    kCompatible,
    kOnlyVideo,
    kPreview,
    kApply,
    kClose,
};

// Client area size in pixels at 100% scaling (96 DPI); scale() converts.
constexpr int kWidth = 520;
constexpr int kHeight = 668;
constexpr int kMargin = 20;

// Windows 11 style colours. The accent is the logo's violet, so the window matches the icon.
constexpr COLORREF kLightBackground = RGB(243, 243, 243);
constexpr COLORREF kDarkBackground = RGB(32, 32, 32);

constexpr wchar_t kLoopTooltip[] =
    L"Memory: a loop of 4 seconds or less keeps a second video player loaded all the time, "
    L"which roughly doubles WallFlow's memory use. Longer loops load it only for the last "
    L"2 seconds of each loop.";

constexpr wchar_t kCompatibleTooltip[] =
    L"Decodes this video with Vulkan instead of DirectX. Turn it on if this video shows blocks "
    L"of wrong colours: some GPU drivers decode some videos wrongly through DirectX. Costs a "
    L"little more (measured with a 4K video on an RTX 3070: about +1.5% CPU, +250 MB).";

// The standard Windows folder picker. COM: CoCreateInstance makes the dialog object;
// ComPtr calls Release() on each interface when it goes out of scope, like a unique_ptr for
// COM's reference counting. `open` points at the dialog while it is shown, so the app can
// close it when quitting.
std::optional<std::filesystem::path> pick_folder(HWND owner, const std::filesystem::path& start,
                                                 IFileDialog*& open) {
    using Microsoft::WRL::ComPtr;
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&dialog)))) {
        return std::nullopt;
    }
    FILEOPENDIALOGOPTIONS options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dialog->SetTitle(L"Choose the folder with your wallpaper videos");
    if (!start.empty()) {
        ComPtr<IShellItem> folder;
        if (SUCCEEDED(::SHCreateItemFromParsingName(start.c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
            dialog->SetFolder(folder.Get());
        }
    }
    // Show() runs its own message loop until the user picks or cancels (the wallpaper keeps
    // playing meanwhile). Cancel returns an error HRESULT.
    open = dialog.Get();
    const HRESULT shown = dialog->Show(owner);
    open = nullptr;
    if (shown != S_OK) {
        return std::nullopt;
    }
    ComPtr<IShellItem> item;
    PWSTR path = nullptr;
    if (FAILED(dialog->GetResult(&item)) || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
        return std::nullopt;
    }
    std::filesystem::path result(path);
    ::CoTaskMemFree(path);  // COM allocated the string; COM's allocator frees it
    return result;
}

HICON load_app_icon(int size) {
    return static_cast<HICON>(::LoadImageW(::GetModuleHandleW(nullptr), MAKEINTRESOURCEW(kAppIconId),
                                           IMAGE_ICON, size, size, LR_DEFAULTCOLOR));
}

}  // namespace

SettingsWindow::SettingsWindow(HINSTANCE instance, ChangeHandler on_change,
                               PreviewHandler on_preview, AudioHandler on_audio, Action on_closed)
    : instance_(instance),
      on_change_(std::move(on_change)),
      on_preview_(std::move(on_preview)),
      on_audio_(std::move(on_audio)),
      on_closed_(std::move(on_closed)) {
    // The list view and the slider live in comctl32; this registers their window classes.
    INITCOMMONCONTROLSEX controls{sizeof controls, ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_TAB_CLASSES};
    ::InitCommonControlsEx(&controls);

    // GDI+ needs a session open while it draws; it is closed in the destructor.
    const Gdiplus::GdiplusStartupInput gdiplus_input;
    Gdiplus::GdiplusStartup(&gdiplus_, &gdiplus_input, nullptr);

    big_icon_ = load_app_icon(::GetSystemMetrics(SM_CXICON));
    small_icon_ = load_app_icon(::GetSystemMetrics(SM_CXSMICON));

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = window_proc<SettingsWindow>;
    wc.hInstance = instance;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = big_icon_;
    wc.hIconSm = small_icon_;
    wc.lpszClassName = kClassName;
    ::RegisterClassExW(&wc);  // fails harmlessly if already registered
}

SettingsWindow::~SettingsWindow() {
    if (hwnd_ != nullptr) {
        ::DestroyWindow(hwnd_);
    }
    ::UnregisterClassW(kClassName, instance_);
    if (gdiplus_ != 0) {
        Gdiplus::GdiplusShutdown(gdiplus_);  // after the window is gone: nothing draws any more
    }
    for (HICON icon : {big_icon_, small_icon_}) {
        if (icon != nullptr) {
            ::DestroyIcon(icon);
        }
    }
}

void SettingsWindow::show(const Settings& settings) {
    if (hwnd_ == nullptr || settings_ == applied_) {
        settings_ = settings;  // (an open window with unapplied edits keeps them)
    }
    applied_ = settings;
    if (hwnd_ == nullptr) {
        create_window();
        ::ShowWindow(hwnd_, SW_SHOW);
    } else {
        fill_from_settings();
        if (::IsIconic(hwnd_)) {
            ::ShowWindow(hwnd_, SW_RESTORE);
        }
    }
    ::SetForegroundWindow(hwnd_);
}

void SettingsWindow::update(const Settings& settings) {
    if (settings == applied_) {
        return;  // our own Apply coming back from the app
    }
    const bool pending = settings_ != applied_;
    applied_ = settings;
    if (pending) {
        // settings.json was edited by hand while the window has unapplied edits: keep the
        // edits (Apply would overwrite the file), but say so.
        if (hwnd_ != nullptr) {
            set_status(L"settings.json changed meanwhile. Apply will replace it with what you see here.", true);
            update_apply_button();
        }
        return;
    }
    settings_ = settings;
    if (hwnd_ != nullptr) {
        fill_from_settings();
    }
}

void SettingsWindow::videos_changed() {
    if (hwnd_ != nullptr) {
        fill_video_list();
    }
}

bool SettingsWindow::handle_dialog_message(MSG& msg) {
    return hwnd_ != nullptr && ::IsDialogMessageW(hwnd_, &msg) != FALSE;
}

int SettingsWindow::scale(int value) const {
    return ::MulDiv(value, static_cast<int>(dpi_), 96);
}

std::wstring SettingsWindow::text_of(HWND control) const {
    std::wstring text(static_cast<std::size_t>(::GetWindowTextLengthW(control)) + 1, L'\0');
    text.resize(static_cast<std::size_t>(::GetWindowTextW(control, text.data(), static_cast<int>(text.size()))));
    return text;
}

// ---- Creating and laying out -----------------------------------------------------------

void SettingsWindow::create_window() {
    constexpr DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    // Created hidden at size 0 first: only then do we know the DPI of the monitor it is on.
    ::CreateWindowExW(WS_EX_CONTROLPARENT, kClassName, L"WallFlow settings", style, CW_USEDEFAULT,
                      CW_USEDEFAULT, 0, 0, nullptr, nullptr, instance_, this);
    if (hwnd_ == nullptr) {
        return;
    }

    // Centre on the monitor with the mouse (where the tray icon was just clicked).
    POINT cursor{};
    ::GetCursorPos(&cursor);
    MONITORINFO monitor{};
    monitor.cbSize = sizeof monitor;
    ::GetMonitorInfoW(::MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY), &monitor);
    // Move onto that monitor first, so GetDpiForWindow reports its DPI.
    ::SetWindowPos(hwnd_, nullptr, monitor.rcWork.left, monitor.rcWork.top, 0, 0,
                   SWP_NOZORDER | SWP_NOACTIVATE);
    dpi_ = ::GetDpiForWindow(hwnd_);

    RECT frame{0, 0, scale(kWidth), scale(kHeight)};
    ::AdjustWindowRectExForDpi(&frame, style, FALSE, WS_EX_CONTROLPARENT, dpi_);
    const int width = frame.right - frame.left;
    const int height = frame.bottom - frame.top;
    const RECT& work = monitor.rcWork;
    ::SetWindowPos(hwnd_, nullptr, work.left + (work.right - work.left - width) / 2,
                   work.top + (work.bottom - work.top - height) / 2, width, height,
                   SWP_NOZORDER | SWP_NOACTIVATE);

    create_fonts();
    create_controls();
    layout();
    apply_theme();  // before the window is first shown, so it never flashes white in dark mode
    fill_from_settings();
    set_status(L"Change what you like, then click Apply.", false);
}

void SettingsWindow::create_fonts() {
    // The font Windows uses for message boxes and dialogs (Segoe UI 9 pt), at this DPI.
    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof metrics;
    ::SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof metrics, &metrics, 0, dpi_);
    font_.reset(::CreateFontIndirectW(&metrics.lfMessageFont));
    LOGFONTW heading = metrics.lfMessageFont;
    heading.lfWeight = FW_SEMIBOLD;
    heading.lfHeight = ::MulDiv(heading.lfHeight, 120, 100);
    heading_font_.reset(::CreateFontIndirectW(&heading));
}

void SettingsWindow::create_controls() {
    auto add = [this](const wchar_t* type, const wchar_t* text, DWORD style, int id = 0,
                      DWORD ex_style = 0) {
        HWND control = ::CreateWindowExW(ex_style, type, text, WS_CHILD | WS_VISIBLE | style, 0, 0,
                                         0, 0, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                         instance_, nullptr);
        ::SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_.get()), FALSE);
        return control;
    };
    constexpr DWORD label = SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX;
    constexpr DWORD field = ES_AUTOHSCROLL | WS_TABSTOP;
    // Toggles are owner-drawn buttons: we paint the switch ourselves (draw_toggle), because a
    // standard checkbox ignores the text colour we set and stays black in dark mode.
    constexpr DWORD toggle = BS_OWNERDRAW | WS_TABSTOP;

    // Created in tab order.
    folder_heading_ = add(WC_STATICW, L"Videos folder", label);
    folder_ = add(WC_EDITW, L"", field | ES_READONLY, kFolder, WS_EX_CLIENTEDGE);
    browse_ = add(WC_BUTTONW, L"Browse...", BS_PUSHBUTTON | WS_TABSTOP, kBrowse);
    interval_label_ = add(WC_STATICW, L"Play each video for", label);
    interval_ = add(WC_EDITW, L"", field, kInterval, WS_EX_CLIENTEDGE);
    interval_hint_ = add(WC_STATICW, L"seconds (at least 10)", label);
    shuffle_ = add(WC_BUTTONW, L"Shuffle (random order, each video once per round)", toggle, kShuffle);
    mute_ = add(WC_BUTTONW, L"Mute", toggle, kMute);
    volume_label_ = add(WC_STATICW, L"Volume", label);
    volume_ = add(TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | TBS_BOTH | WS_TABSTOP, kVolume);
    ::SendMessageW(volume_, TBM_SETRANGE, FALSE, MAKELPARAM(0, 100));
    ::SendMessageW(volume_, TBM_SETPAGESIZE, 0, 10);
    volume_value_ = add(WC_STATICW, L"", label);
    autostart_ = add(WC_BUTTONW, L"Start with Windows", toggle, kStartWithWindows);

    videos_heading_ = add(WC_STATICW, L"Videos", label);
    list_ = add(WC_LISTVIEWW, L"",
                LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER | WS_TABSTOP, kVideoList);
    ListView_SetExtendedListViewStyle(list_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    const wchar_t* columns[] = {L"Video", L"Loop start", L"Loop end", L"Options"};
    for (int i = 0; i < 4; ++i) {
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT;
        column.pszText = const_cast<wchar_t*>(columns[i]);  // the API is not const-correct
        ListView_InsertColumn(list_, i, &column);
    }
    // The header (column titles) belongs to the list, and so do its notifications; this
    // "subclass" lets us see them (to colour the header text in dark mode).
    ::SetWindowSubclass(list_, &SettingsWindow::list_proc, 1, reinterpret_cast<DWORD_PTR>(this));
    ::SetWindowSubclass(volume_, &SettingsWindow::slider_proc, 1, reinterpret_cast<DWORD_PTR>(this));

    loop_start_label_ = add(WC_STATICW, L"Loop start", label);
    loop_start_ = add(WC_EDITW, L"", field, kLoopStart, WS_EX_CLIENTEDGE);
    loop_end_label_ = add(WC_STATICW, L"Loop end", label);
    loop_end_ = add(WC_EDITW, L"", field, kLoopEnd, WS_EX_CLIENTEDGE);
    whole_video_ = add(WC_BUTTONW, L"Whole video", BS_PUSHBUTTON | WS_TABSTOP, kWholeVideo);
    compatible_ = add(WC_BUTTONW, L"Compatible decoding (if this video shows glitches)", toggle, kCompatible);
    preview_ = add(WC_BUTTONW, L"Preview", BS_PUSHBUTTON | WS_TABSTOP, kPreview);
    only_ = add(WC_BUTTONW, L"Play only this video (no rotation)", toggle, kOnlyVideo);
    loop_hint_ = add(WC_STATICW,
                     L"Pick a video, then type its loop start and end in seconds (2, 4.5) or as 1:30.",
                     label);
    status_ = add(WC_STATICW, L"", label);
    version_ = add(WC_STATICW, (L"Version " + std::wstring(version())).c_str(), label);
    apply_ = add(WC_BUTTONW, L"Apply", BS_PUSHBUTTON | WS_TABSTOP, kApply);
    close_ = add(WC_BUTTONW, L"Close", BS_PUSHBUTTON | WS_TABSTOP, kClose);

    for (HWND heading : {folder_heading_, videos_heading_}) {
        ::SendMessageW(heading, WM_SETFONT, reinterpret_cast<WPARAM>(heading_font_.get()), FALSE);
    }

    // The memory note, shown when the mouse rests on the loop fields.
    tooltip_ = ::CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                                 WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0, 0, 0, hwnd_, nullptr,
                                 instance_, nullptr);
    ::SendMessageW(tooltip_, TTM_SETMAXTIPWIDTH, 0, scale(320));
    ::SendMessageW(tooltip_, TTM_SETDELAYTIME, TTDT_AUTOPOP, 20000);
    for (HWND tool : {loop_start_label_, loop_start_, loop_end_label_, loop_end_}) {
        TOOLINFOW info{};
        info.cbSize = sizeof info;
        // TTF_IDISHWND: the tool is the whole control; TTF_SUBCLASS: the tooltip watches the
        // control's mouse messages itself.
        info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        info.hwnd = hwnd_;
        info.uId = reinterpret_cast<UINT_PTR>(tool);
        info.lpszText = const_cast<wchar_t*>(kLoopTooltip);
        ::SendMessageW(tooltip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
    }
    {
        TOOLINFOW info{};
        info.cbSize = sizeof info;
        info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        info.hwnd = hwnd_;
        info.uId = reinterpret_cast<UINT_PTR>(compatible_);
        info.lpszText = const_cast<wchar_t*>(kCompatibleTooltip);
        ::SendMessageW(tooltip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
    }
    // Labels only get mouse messages (and so tooltips) with SS_NOTIFY.
    for (HWND tool : {loop_start_label_, loop_end_label_}) {
        ::SetWindowLongPtrW(tool, GWL_STYLE, ::GetWindowLongPtrW(tool, GWL_STYLE) | SS_NOTIFY);
    }
}

void SettingsWindow::layout() {
    // Positions at 96 DPI, scaled. One place for the whole layout, so a DPI change (window
    // dragged to another monitor) just runs this again.
    auto place = [this](HWND control, int x, int y, int width, int height) {
        ::MoveWindow(control, scale(x), scale(y), scale(width), scale(height), FALSE);
    };
    constexpr int m = kMargin;
    constexpr int right = kWidth - kMargin;
    constexpr int row = 26;

    place(folder_heading_, m, 14, 300, 24);
    place(folder_, m, 42, 372, row);
    place(browse_, right - 96, 41, 96, 28);

    place(interval_label_, m, 84, 132, row);
    place(interval_, m + 136, 84, 70, row);
    place(interval_hint_, m + 216, 84, 260, row);
    place(shuffle_, m, 118, 420, row);
    place(mute_, m, 150, 110, row);
    place(volume_label_, m + 130, 150, 56, row);
    place(volume_, m + 186, 150, 230, row);
    place(volume_value_, m + 424, 150, 40, row);
    place(autostart_, m, 182, 300, row);

    place(videos_heading_, m, 224, 400, 24);
    place(list_, m, 252, right - m, 196);
    place(loop_start_label_, m, 460, 70, row);
    place(loop_start_, m + 72, 460, 80, row);
    place(loop_end_label_, m + 170, 460, 62, row);
    place(loop_end_, m + 234, 460, 80, row);
    place(whole_video_, right - 110, 459, 110, 28);
    place(compatible_, m, 492, 360, row);
    place(preview_, right - 110, 491, 110, 28);
    place(only_, m, 524, 360, row);
    place(loop_hint_, m, 556, right - m, 22);

    place(status_, m, 586, right - m, 22);
    place(version_, m, 629, 160, 22);
    place(apply_, right - 96 - 10 - 96, 624, 96, 30);
    place(close_, right - 96, 624, 96, 30);

    // Columns: the loop columns fixed, the name takes what is left (minus a scrollbar).
    RECT list{};
    ::GetClientRect(list_, &list);
    const int time_column = scale(76);
    const int decoding_column = scale(110);
    const int scrollbar = ::GetSystemMetricsForDpi(SM_CXVSCROLL, dpi_);
    ListView_SetColumnWidth(list_, 0, std::max(scale(100), static_cast<int>(list.right) - 2 * time_column -
                                                                decoding_column - scrollbar));
    ListView_SetColumnWidth(list_, 1, time_column);
    ListView_SetColumnWidth(list_, 2, time_column);
    ListView_SetColumnWidth(list_, 3, decoding_column);

    // Inner padding for the text fields.
    for (HWND edit : {folder_, interval_, loop_start_, loop_end_}) {
        ::SendMessageW(edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(scale(4), scale(4)));
    }
    ::InvalidateRect(hwnd_, nullptr, TRUE);
}

// ---- Light and dark ---------------------------------------------------------------------

void SettingsWindow::apply_theme() {
    dark_ = apps_use_dark_theme();
    palette_ = dark_ ? Palette{kDarkBackground, RGB(255, 255, 255), RGB(170, 170, 170), RGB(45, 45, 45),
                               RGB(95, 95, 95), RGB(150, 128, 255), RGB(255, 255, 255), RGB(255, 153, 164)}
                     : Palette{kLightBackground, RGB(26, 26, 26), RGB(96, 96, 96), RGB(255, 255, 255),
                               RGB(190, 190, 190), RGB(98, 70, 234), RGB(255, 255, 255), RGB(196, 43, 28)};
    background_brush_.reset(::CreateSolidBrush(palette_.background));
    field_brush_.reset(::CreateSolidBrush(palette_.field));

    set_dark_title_bar(hwnd_, dark_);
    // Theme classes with a "DarkMode_" prefix are the dark versions of the standard ones,
    // the same ones File Explorer uses. nullptr = back to the control's normal theme.
    const wchar_t* button_theme = dark_ ? L"DarkMode_Explorer" : nullptr;
    for (HWND button : {browse_, whole_video_, preview_, apply_, close_}) {
        ::SetWindowTheme(button, button_theme, nullptr);
    }
    for (HWND edit : {folder_, interval_, loop_start_, loop_end_}) {
        ::SetWindowTheme(edit, dark_ ? L"DarkMode_CFD" : nullptr, nullptr);
    }
    ::SetWindowTheme(list_, dark_ ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    ::SetWindowTheme(ListView_GetHeader(list_), dark_ ? L"DarkMode_ItemsView" : L"ItemsView", nullptr);
    ::SetWindowTheme(tooltip_, dark_ ? L"DarkMode_Explorer" : nullptr, nullptr);
    // The slider caches its background; a theme reset (WM_THEMECHANGED) makes it drop that.
    ::SetWindowTheme(volume_, nullptr, nullptr);
    ListView_SetBkColor(list_, palette_.field);
    ListView_SetTextBkColor(list_, palette_.field);
    ListView_SetTextColor(list_, palette_.text);

    ::RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
}

// ---- Showing the settings ---------------------------------------------------------------

void SettingsWindow::fill_from_settings() {
    ::SetWindowTextW(folder_, settings_.folder.empty() ? L"(none chosen)" : settings_.folder.c_str());
    ::SetWindowTextW(interval_, seconds_text(settings_.interval).c_str());
    ::SendMessageW(volume_, TBM_SETPOS, TRUE, settings_.volume);
    ::SetWindowTextW(volume_value_, std::to_wstring(settings_.volume).c_str());
    ::EnableWindow(volume_, !settings_.mute);
    for (HWND toggle : {shuffle_, mute_, autostart_}) {
        ::InvalidateRect(toggle, nullptr, FALSE);
    }
    fill_video_list();
    update_apply_button();
}

void SettingsWindow::update_apply_button() {
    ::EnableWindow(apply_, settings_ != applied_);
}

void SettingsWindow::fill_video_list() {
    // Keep the selection on the same video across a refresh.
    const int selected = selected_row();
    const std::wstring selected_name = selected >= 0 ? rows_[selected].name : std::wstring();

    rows_ = video_rows(settings_.folder.empty() ? std::vector<std::filesystem::path>{}
                                                : scan_folder(settings_.folder),
                       settings_);
    ::SendMessageW(list_, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(list_);
    int reselect = -1;
    for (int i = 0; i < static_cast<int>(rows_.size()); ++i) {
        VideoRow& row = rows_[i];
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = i;
        item.pszText = row.name.data();
        ListView_InsertItem(list_, &item);
        ListView_SetItemText(list_, i, 1, row.start.data());
        ListView_SetItemText(list_, i, 2, row.end.data());
        std::wstring options = options_text(row);
        ListView_SetItemText(list_, i, 3, options.data());
        if (row.name == selected_name) {
            reselect = i;
        }
    }
    if (reselect >= 0) {
        ListView_SetItemState(list_, reselect, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
    ::SendMessageW(list_, WM_SETREDRAW, TRUE, 0);
    ::InvalidateRect(list_, nullptr, TRUE);

    std::wstring heading = L"Videos";
    if (settings_.folder.empty()) {
        heading += L" (choose a folder first)";
    } else if (rows_.empty()) {
        heading += L" (none in this folder)";
    } else {
        heading += L" in this folder (" + std::to_wstring(rows_.size()) + L")";
    }
    ::SetWindowTextW(videos_heading_, heading.c_str());
    show_selected_loop();
    update_rotation_controls();
}

int SettingsWindow::selected_row() const {
    const int row = ListView_GetNextItem(list_, -1, LVNI_SELECTED);
    return row >= 0 && row < static_cast<int>(rows_.size()) ? row : -1;
}

void SettingsWindow::show_selected_loop() {
    const int row = selected_row();
    const bool any = row >= 0;
    ::SetWindowTextW(loop_start_, any ? rows_[row].start.c_str() : L"");
    ::SetWindowTextW(loop_end_, any ? rows_[row].end.c_str() : L"");
    for (HWND control : {loop_start_, loop_end_, whole_video_, compatible_, preview_, only_}) {
        ::EnableWindow(control, any);
    }
    ::InvalidateRect(compatible_, nullptr, FALSE);
    ::InvalidateRect(only_, nullptr, FALSE);
}

void SettingsWindow::set_status(const std::wstring& text, bool error) {
    status_error_ = error;
    ::SetWindowTextW(status_, text.c_str());
    ::InvalidateRect(status_, nullptr, TRUE);
}

bool SettingsWindow::toggle_on(int id) const {
    switch (id) {
        case kShuffle:
            return settings_.shuffle;
        case kMute:
            return settings_.mute;
        case kStartWithWindows:
            return settings_.start_with_windows;
        case kCompatible: {
            const int row = selected_row();
            return row >= 0 && rows_[row].compatible;
        }
        case kOnlyVideo: {
            const int row = selected_row();
            return row >= 0 && rows_[row].only;
        }
        default:
            return false;
    }
}

// ---- Changing the settings --------------------------------------------------------------

void SettingsWindow::changed(const std::wstring& status) {
    update_apply_button();
    set_status(settings_ == applied_ ? std::wstring() : status + L" Click Apply to use it.", false);
}

bool SettingsWindow::apply() {
    // Fields that were typed in but not left yet count too.
    if (!commit_interval() || !commit_loop()) {
        return false;  // the red message says what is wrong; nothing is applied
    }
    if (settings_ == applied_) {
        set_status(L"Nothing to apply.", false);
        update_apply_button();
        return true;
    }
    applied_ = settings_;
    on_change_(settings_);
    update_apply_button();
    set_status(L"Applied.", false);
    return true;
}

void SettingsWindow::close_asking() {
    // Also catches text typed into a field that was not left yet.
    const bool fields_ok = commit_interval() && commit_loop();
    if (!fields_ok || settings_ != applied_) {
        const int answer = ::MessageBoxW(hwnd_, L"Apply your changes before closing?", L"WallFlow settings",
                                         MB_YESNOCANCEL | MB_ICONQUESTION);
        if (answer == IDCANCEL || (answer == IDYES && !apply())) {
            return;  // stay open (after a failed Apply the red message says why)
        }
        if (answer == IDNO) {
            settings_ = applied_;  // discard
        }
    }
    ::DestroyWindow(hwnd_);
}

void SettingsWindow::on_command(int id, int code) {
    switch (id) {
        case kBrowse:
            browse_folder();
            break;
        case kShuffle:
        case kMute:
        case kStartWithWindows:
        case kCompatible:
        case kOnlyVideo:
            // A quick second click on an owner-drawn button arrives as a double-click.
            if (code == BN_CLICKED || code == BN_DOUBLECLICKED) {
                toggle(id);
            }
            break;
        case kInterval:
            if (code == EN_KILLFOCUS) {
                commit_interval();
            }
            break;
        case kLoopStart:
        case kLoopEnd:
            if (code == EN_KILLFOCUS) {
                ::PostMessageW(hwnd_, kLoopFocusMessage, 0, 0);
            }
            break;
        case kWholeVideo:
            clear_loop();
            break;
        case kPreview:
            preview();
            break;
        case IDOK:  // Enter (sent by IsDialogMessage)
        case kApply:
            apply();
            break;
        case IDCANCEL:  // Esc
        case kClose:
            close_asking();
            break;
        default:
            break;
    }
}

void SettingsWindow::toggle(int id) {
    std::wstring status;
    switch (id) {
        case kShuffle:
            settings_.shuffle = !settings_.shuffle;
            status = settings_.shuffle ? L"Shuffle on." : L"Shuffle off: videos play in name order.";
            break;
        case kMute:
            settings_.mute = !settings_.mute;
            ::EnableWindow(volume_, !settings_.mute);
            if (on_audio_) {
                on_audio_(settings_.mute, settings_.volume);  // hear it now
            }
            status = settings_.mute ? L"Muted (the audio is not even decoded)." : L"Sound on.";
            break;
        case kStartWithWindows:
            settings_.start_with_windows = !settings_.start_with_windows;
            status = settings_.start_with_windows ? L"WallFlow will start when you log in."
                                                  : L"WallFlow will not start by itself.";
            break;
        case kCompatible: {
            const int row = selected_row();
            if (row < 0) {
                return;
            }
            VideoRow& video = rows_[row];
            video.compatible = !video.compatible;
            set_compatible(settings_, video.name, video.compatible);
            std::wstring options = options_text(video);
            ListView_SetItemText(list_, row, 3, options.data());
            status = video.compatible ? L"Compatible decoding for " + video.name + L"."
                                      : L"Normal decoding for " + video.name + L".";
            break;
        }
        case kOnlyVideo: {
            const int row = selected_row();
            if (row < 0) {
                return;
            }
            const bool on = !rows_[row].only;
            settings_.only_video = on ? rows_[row].name : std::wstring();
            // At most one video plays alone: update every row's mark.
            for (int i = 0; i < static_cast<int>(rows_.size()); ++i) {
                rows_[i].only = on && i == row;
                std::wstring options = options_text(rows_[i]);
                ListView_SetItemText(list_, i, 3, options.data());
            }
            update_rotation_controls();
            status = on ? std::wstring(L"Only this video will play, looping.")
                        : std::wstring(L"All videos in the folder will play again.");
            break;
        }
        default:
            return;
    }
    ::InvalidateRect(::GetDlgItem(hwnd_, id), nullptr, FALSE);
    changed(status);
}

bool SettingsWindow::commit_interval() {
    const IntervalField field = read_interval(text_of(interval_));
    if (!field.seconds) {
        set_status(field.error, true);
        return false;
    }
    ::SetWindowTextW(interval_, seconds_text(*field.seconds).c_str());  // "5:00" -> "300"
    if (*field.seconds != settings_.interval) {
        settings_.interval = *field.seconds;
        changed(L"Each video plays for " + seconds_text(*field.seconds) + L" seconds.");
    }
    return true;
}

bool SettingsWindow::commit_loop() {
    const int row = selected_row();
    if (row < 0) {
        return true;
    }
    const LoopFields fields = read_loop(text_of(loop_start_), text_of(loop_end_));
    if (!fields.loop) {
        set_status(fields.error, true);
        return false;
    }
    Settings next = settings_;
    set_loop(next, rows_[row].name, *fields.loop);
    // Show the times as stored ("2" -> "0:02").
    ::SetWindowTextW(loop_start_, time_text(fields.loop->start).c_str());
    ::SetWindowTextW(loop_end_, time_text(fields.loop->end).c_str());
    if (next == settings_) {
        if (status_error_) {
            set_status(L"", false);  // the mistake was fixed by typing the old value again
        }
        return true;
    }
    settings_ = std::move(next);
    rows_[row].start = time_text(fields.loop->start);
    rows_[row].end = time_text(fields.loop->end);
    ListView_SetItemText(list_, row, 1, rows_[row].start.data());
    ListView_SetItemText(list_, row, 2, rows_[row].end.data());
    changed(fields.loop->start || fields.loop->end ? L"New loop for " + rows_[row].name + L"."
                                                    : rows_[row].name + L" loops as a whole.");
    return true;
}

void SettingsWindow::clear_loop() {
    ::SetWindowTextW(loop_start_, L"");
    ::SetWindowTextW(loop_end_, L"");
    commit_loop();
}

void SettingsWindow::cancel_folder_picker() {
    if (picker_ != nullptr) {
        picker_->Close(HRESULT_FROM_WIN32(ERROR_CANCELLED));  // Show() returns this
    }
}

void SettingsWindow::update_rotation_controls() {
    const bool alone = std::any_of(rows_.begin(), rows_.end(), [](const VideoRow& row) { return row.only; });
    for (HWND control : {interval_, shuffle_}) {
        ::EnableWindow(control, !alone);
        ::InvalidateRect(control, nullptr, FALSE);
    }
}

void SettingsWindow::preview() {
    const int row = selected_row();
    if (row < 0 || !commit_loop()) {
        return;  // nothing selected, or the loop fields have a mistake (shown in red)
    }
    const VideoRow& video = rows_[row];
    on_preview_(settings_.folder / video.name, settings_.loop_for(video.name), video.compatible);
    set_status(L"Showing " + video.name + L" on the desktop. Apply or Close to go back.", false);
}

void SettingsWindow::browse_folder() {
    const auto folder = pick_folder(hwnd_, settings_.folder, picker_);
    if (!folder || *folder == settings_.folder) {
        return;
    }
    settings_.folder = *folder;
    ::SetWindowTextW(folder_, settings_.folder.c_str());
    fill_video_list();
    changed(L"New folder.");
}

// ---- Painting ---------------------------------------------------------------------------

void SettingsWindow::draw_toggle(const DRAWITEMSTRUCT& item) {
    HDC dc = item.hDC;
    const RECT& area = item.rcItem;
    ::FillRect(dc, &area, background_brush_.get());

    // A switch like Windows 11's: a pill, filled with the accent colour when on, with a knob
    // that sits on the right when on and on the left when off.
    const bool on = toggle_on(static_cast<int>(item.CtlID));
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const int height = scale(20);
    const int width = scale(40);
    const int top = area.top + (area.bottom - area.top - height) / 2;
    const RECT pill{area.left + scale(1), top, area.left + scale(1) + width, top + height};

    const COLORREF accent = disabled ? palette_.track : palette_.accent;
    const COLORREF line = disabled ? palette_.track : palette_.secondary;
    {
        Gdiplus::Graphics g(dc);
        smooth(g);
        // The outline is centred on the path: inset by half its width so it stays inside.
        const float pen = static_cast<float>(std::max(1, scale(1)));
        const float x = static_cast<float>(pill.left) + pen / 2;
        const float y = static_cast<float>(pill.top) + pen / 2;
        const float w = static_cast<float>(width) - pen;
        const float h = static_cast<float>(height) - pen;
        // A pill: two half circles joined by straight lines (CloseFigure adds them).
        Gdiplus::GraphicsPath path;
        path.AddArc(x, y, h, h, 90, 180);
        path.AddArc(x + w - h, y, h, h, 270, 180);
        path.CloseFigure();
        Gdiplus::SolidBrush fill(to_gdiplus(on ? accent : palette_.background));
        g.FillPath(&fill, &path);
        Gdiplus::Pen outline(to_gdiplus(on ? accent : line), pen);
        g.DrawPath(&outline, &path);

        const float knob = static_cast<float>(height - scale(10));
        const float knob_left = on ? static_cast<float>(pill.right - scale(5)) - knob
                                   : static_cast<float>(pill.left + scale(5));
        const float knob_top = static_cast<float>(pill.top) + (static_cast<float>(height) - knob) / 2;
        Gdiplus::SolidBrush knob_fill(to_gdiplus(on ? palette_.knob : line));
        g.FillEllipse(&knob_fill, knob_left, knob_top, knob, knob);
    }  // the Graphics object is done with the DC before GDI draws the text

    const std::wstring text = text_of(item.hwndItem);
    RECT text_area{pill.right + scale(10), area.top, area.right, area.bottom};
    HGDIOBJ old_font = ::SelectObject(dc, font_.get());
    ::SetBkMode(dc, TRANSPARENT);
    ::SetTextColor(dc, disabled ? palette_.secondary : palette_.text);
    ::DrawTextW(dc, text.c_str(), -1, &text_area, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
    if ((item.itemState & ODS_FOCUS) != 0 && (item.itemState & ODS_NOFOCUSRECT) == 0) {
        RECT focus = text_area;
        ::DrawTextW(dc, text.c_str(), -1, &focus, DT_SINGLELINE | DT_CALCRECT | DT_NOPREFIX);
        focus.top = area.top + scale(2);
        focus.bottom = area.bottom - scale(2);
        ::InflateRect(&focus, scale(3), 0);
        ::DrawFocusRect(dc, &focus);
    }
    ::SelectObject(dc, old_font);
}

LRESULT SettingsWindow::draw_slider(const NMCUSTOMDRAW& draw) {
    // The slider asks before painting each part; we paint the track and the knob ourselves
    // (the standard ones are light grey in both themes) and leave the rest to it.
    if (draw.dwDrawStage == CDDS_PREPAINT) {
        // Our own background too: the slider keeps a copy of the brush it got from
        // WM_CTLCOLORSTATIC, so after a light/dark switch it would paint the old colour.
        ::FillRect(draw.hdc, &draw.rc, background_brush_.get());
        return CDRF_NOTIFYITEMDRAW;
    }
    if (draw.dwDrawStage != CDDS_ITEMPREPAINT) {
        return CDRF_DODEFAULT;
    }
    const bool enabled = ::IsWindowEnabled(volume_) != FALSE;
    const COLORREF accent = enabled ? palette_.accent : palette_.track;
    if (draw.dwItemSpec == TBCD_CHANNEL) {
        RECT thumb{};
        ::SendMessageW(volume_, TBM_GETTHUMBRECT, 0, reinterpret_cast<LPARAM>(&thumb));
        const int middle = (draw.rc.top + draw.rc.bottom) / 2;
        const int half = std::max(1, scale(2));
        const int split = (thumb.left + thumb.right) / 2;
        const RECT done{draw.rc.left, middle - half, split, middle + half};
        const RECT rest{split, middle - half, draw.rc.right, middle + half};
        UniqueGdi<HBRUSH> done_fill(::CreateSolidBrush(accent));
        UniqueGdi<HBRUSH> rest_fill(::CreateSolidBrush(palette_.track));
        ::FillRect(draw.hdc, &done, done_fill.get());
        ::FillRect(draw.hdc, &rest, rest_fill.get());
        return CDRF_SKIPDEFAULT;
    }
    if (draw.dwItemSpec == TBCD_THUMB) {
        const RECT& r = draw.rc;
        const int size = std::min(r.right - r.left, r.bottom - r.top) - scale(2);
        const int left = (r.left + r.right - size) / 2;
        const int top = (r.top + r.bottom - size) / 2;
        // A ring in the field colour with an accent dot inside, like Windows 11's slider.
        Gdiplus::Graphics g(draw.hdc);
        smooth(g);
        const auto outer = static_cast<float>(size);
        const auto ring = static_cast<float>(scale(3));
        Gdiplus::SolidBrush ring_fill(to_gdiplus(palette_.field));
        g.FillEllipse(&ring_fill, static_cast<float>(left), static_cast<float>(top), outer, outer);
        Gdiplus::SolidBrush dot(to_gdiplus(accent));
        g.FillEllipse(&dot, static_cast<float>(left) + ring, static_cast<float>(top) + ring,
                      outer - 2 * ring, outer - 2 * ring);
        return CDRF_SKIPDEFAULT;
    }
    return CDRF_DODEFAULT;
}

LRESULT CALLBACK SettingsWindow::list_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam,
                                           UINT_PTR, DWORD_PTR self_data) {
    auto* self = reinterpret_cast<SettingsWindow*>(self_data);
    if (msg == WM_NOTIFY) {
        // The header's "about to paint" notifications: give the column titles our text colour.
        const auto* header = reinterpret_cast<const NMHDR*>(lparam);
        if (header->hwndFrom == ListView_GetHeader(hwnd) && header->code == NM_CUSTOMDRAW) {
            const auto* draw = reinterpret_cast<const NMCUSTOMDRAW*>(lparam);
            if (draw->dwDrawStage == CDDS_PREPAINT) {
                return CDRF_NOTIFYITEMDRAW;
            }
            if (draw->dwDrawStage == CDDS_ITEMPREPAINT) {
                ::SetTextColor(draw->hdc, self->palette_.text);
                return CDRF_DODEFAULT;
            }
        }
    } else if (msg == WM_NCDESTROY) {
        ::RemoveWindowSubclass(hwnd, &SettingsWindow::list_proc, 1);
    }
    return ::DefSubclassProc(hwnd, msg, wparam, lparam);
}

LRESULT CALLBACK SettingsWindow::slider_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam,
                                             UINT_PTR, DWORD_PTR self_data) {
    auto* self = reinterpret_cast<SettingsWindow*>(self_data);
    if (msg == WM_LBUTTONDOWN) {
        // A standard slider moves one page (10) towards a click on its track. Like Windows 11's
        // sliders, jump to the clicked point instead. The knob is then under the mouse, so the
        // default handling below starts a drag from there.
        RECT channel{};
        RECT thumb{};
        ::SendMessageW(hwnd, TBM_GETCHANNELRECT, 0, reinterpret_cast<LPARAM>(&channel));
        ::SendMessageW(hwnd, TBM_GETTHUMBRECT, 0, reinterpret_cast<LPARAM>(&thumb));
        const POINT click{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        if (::PtInRect(&thumb, click) == FALSE) {
            // The knob's centre travels from channel.left + half a knob to channel.right - half.
            const int half = (thumb.right - thumb.left) / 2;
            const int from = channel.left + half;
            const int span = std::max(1, static_cast<int>(channel.right - channel.left) - 2 * half);
            const auto low = static_cast<int>(::SendMessageW(hwnd, TBM_GETRANGEMIN, 0, 0));
            const auto high = static_cast<int>(::SendMessageW(hwnd, TBM_GETRANGEMAX, 0, 0));
            const int clamped = std::clamp(static_cast<int>(click.x) - from, 0, span);
            const int position = low + static_cast<int>(std::lround(
                                           static_cast<double>(clamped) * (high - low) / span));
            ::SendMessageW(hwnd, TBM_SETPOS, TRUE, position);
            self->volume_moved();
        }
    } else if (msg == WM_NCDESTROY) {
        ::RemoveWindowSubclass(hwnd, &SettingsWindow::slider_proc, 1);
    }
    return ::DefSubclassProc(hwnd, msg, wparam, lparam);
}

void SettingsWindow::volume_moved() {
    // The slider itself repaints only where the knob was and is; our track is coloured up to
    // the knob, so the whole control must be repainted or the old colour stays behind.
    ::InvalidateRect(volume_, nullptr, FALSE);
    const auto volume = static_cast<int>(::SendMessageW(volume_, TBM_GETPOS, 0, 0));
    if (volume != settings_.volume) {
        settings_.volume = volume;
        ::SetWindowTextW(volume_value_, std::to_wstring(volume).c_str());
        if (on_audio_) {
            on_audio_(settings_.mute, settings_.volume);  // hear it now
        }
        changed(L"Volume " + std::to_wstring(volume) + L". Click Apply to keep it.");
    }
}

// ---- Messages ---------------------------------------------------------------------------

LRESULT SettingsWindow::on_message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_NCCREATE:
            hwnd_ = hwnd;
            break;

        case WM_ERASEBKGND: {
            RECT client{};
            ::GetClientRect(hwnd, &client);
            ::FillRect(reinterpret_cast<HDC>(wparam), &client, background_brush_.get());
            return 1;
        }

        // Before a control paints, it asks its parent for colours and a background brush.
        case WM_CTLCOLORSTATIC: {  // labels, the slider, and read-only text fields
            HDC dc = reinterpret_cast<HDC>(wparam);
            const auto control = reinterpret_cast<HWND>(lparam);
            if (control == folder_) {
                ::SetTextColor(dc, palette_.text);
                ::SetBkColor(dc, palette_.field);
                return reinterpret_cast<LRESULT>(field_brush_.get());
            }
            COLORREF color = palette_.text;
            if (control == status_) {
                color = status_error_ ? palette_.error : palette_.secondary;
            } else if (control == interval_hint_ || control == loop_hint_ || control == version_) {
                color = palette_.secondary;
            }
            ::SetTextColor(dc, color);
            ::SetBkColor(dc, palette_.background);
            return reinterpret_cast<LRESULT>(background_brush_.get());
        }
        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(wparam);
            ::SetTextColor(dc, palette_.text);
            ::SetBkColor(dc, palette_.field);
            return reinterpret_cast<LRESULT>(field_brush_.get());
        }
        case WM_CTLCOLORBTN:  // the corners around rounded buttons
            return reinterpret_cast<LRESULT>(background_brush_.get());

        case WM_DRAWITEM:
            draw_toggle(*reinterpret_cast<const DRAWITEMSTRUCT*>(lparam));
            return TRUE;

        case WM_COMMAND:
            on_command(LOWORD(wparam), HIWORD(wparam));
            return 0;

        case kLoopFocusMessage: {
            const HWND focus = ::GetFocus();
            if (focus != loop_start_ && focus != loop_end_) {
                commit_loop();
            }
            return 0;
        }

        case WM_NOTIFY: {
            const auto* header = reinterpret_cast<const NMHDR*>(lparam);
            if (header->hwndFrom == volume_ && header->code == NM_CUSTOMDRAW) {
                return draw_slider(*reinterpret_cast<const NMCUSTOMDRAW*>(lparam));
            }
            if (header->hwndFrom == list_ && header->code == LVN_ITEMCHANGED) {
                const auto* change = reinterpret_cast<const NMLISTVIEW*>(lparam);
                if ((change->uChanged & LVIF_STATE) != 0 &&
                    ((change->uNewState ^ change->uOldState) & LVIS_SELECTED) != 0) {
                    show_selected_loop();
                }
            }
            break;
        }

        case WM_HSCROLL:
            if (reinterpret_cast<HWND>(lparam) == volume_) {
                volume_moved();
            }
            return 0;

        case WM_SETTINGCHANGE:
            if (is_theme_change(lparam)) {
                apply_theme();  // the user switched light/dark: follow at once
            }
            break;

        case WM_DPICHANGED: {
            // Dragged to a monitor with other scaling: Windows suggests the new window rectangle.
            dpi_ = HIWORD(wparam);
            create_fonts();
            for (HWND child = ::GetWindow(hwnd, GW_CHILD); child != nullptr; child = ::GetWindow(child, GW_HWNDNEXT)) {
                const bool heading = child == folder_heading_ || child == videos_heading_;
                ::SendMessageW(child, WM_SETFONT,
                               reinterpret_cast<WPARAM>(heading ? heading_font_.get() : font_.get()), FALSE);
            }
            const auto* suggested = reinterpret_cast<const RECT*>(lparam);
            ::SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                           suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
            layout();
            return 0;
        }

        case WM_CLOSE:  // the title bar's X
            close_asking();
            return 0;

        case WM_DESTROY:
            if (on_closed_) {
                on_closed_();  // e.g. end a preview
            }
            break;

        case WM_NCDESTROY:
            hwnd_ = nullptr;  // the object stays; show() creates a new window next time
            break;

        default:
            break;
    }
    return ::DefWindowProcW(hwnd, msg, wparam, lparam);
}

}  // namespace wallflow::win
