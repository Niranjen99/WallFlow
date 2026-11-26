#include "wallflow/desktop_layout.hpp"

#include <string_view>
#include <vector>

namespace wallflow {

namespace {

constexpr std::wstring_view kProgman = L"Progman";
constexpr std::wstring_view kWorkerW = L"WorkerW";
constexpr std::wstring_view kIcons = L"SHELLDLL_DefView";

const WindowInfo* find_child(std::span<const WindowInfo> windows, std::uintptr_t parent,
                             std::wstring_view class_name) {
    for (const auto& w : windows) {
        if (w.parent == parent && w.class_name == class_name) {
            return &w;
        }
    }
    return nullptr;
}

}  // namespace

std::optional<AttachPlan> plan_attach(std::span<const WindowInfo> windows) {
    std::vector<const WindowInfo*> top_level;
    for (const auto& w : windows) {
        if (w.parent == 0) {
            top_level.push_back(&w);
        }
    }

    // Classic: find the top-level WorkerW holding the icons; the wallpaper layer is the next
    // top-level WorkerW after it in z-order (directly behind it on screen).
    for (std::size_t i = 0; i < top_level.size(); ++i) {
        const WindowInfo& w = *top_level[i];
        if (w.class_name != kWorkerW || find_child(windows, w.handle, kIcons) == nullptr) {
            continue;
        }
        for (std::size_t j = i + 1; j < top_level.size(); ++j) {
            if (top_level[j]->class_name == kWorkerW) {
                return AttachPlan{DesktopLayout::Classic, top_level[j]->handle, 0};
            }
        }
        return std::nullopt;  // icons found, but no wallpaper layer behind them yet
    }

    // Modern: Progman holds both the icons and the wallpaper layer. Our window goes directly
    // below the icons, so it covers the static wallpaper but not the icons.
    for (const WindowInfo* w : top_level) {
        if (w->class_name != kProgman) {
            continue;
        }
        const WindowInfo* icons = find_child(windows, w->handle, kIcons);
        const WindowInfo* layer = find_child(windows, w->handle, kWorkerW);
        if (icons != nullptr && layer != nullptr) {
            return AttachPlan{DesktopLayout::Modern, w->handle, icons->handle};
        }
    }
    return std::nullopt;
}

}  // namespace wallflow
