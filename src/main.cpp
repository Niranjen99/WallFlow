#include <windows.h>

#include <exception>
#include <memory>
#include <string>

#include "wallflow/win/app.hpp"

namespace {

// A HANDLE is a void*, so this unique_ptr owns a void* and frees it with CloseHandle.
using UniqueHandle = std::unique_ptr<void, decltype(&::CloseHandle)>;

void show_error(const std::string& what) {
    std::wstring text;  // our messages are plain ASCII, so each char maps to one wchar_t
    for (const char c : what) {
        text.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
    }
    ::MessageBoxW(nullptr, text.c_str(), L"WallFlow", MB_OK | MB_ICONERROR);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    // Work in real pixels on every monitor. Without this, Windows scales our window on
    // high-DPI screens and it would not line up with the desktop. Must happen before any
    // window is created.
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // One copy at a time: a second copy would stack a second wallpaper window on the first.
    // The mutex name is visible to all processes in this login session (Local\).
    UniqueHandle single(::CreateMutexW(nullptr, FALSE, L"Local\\WallFlow.SingleInstance"),
                        &::CloseHandle);
    if (!single || ::GetLastError() == ERROR_ALREADY_EXISTS) {
        return 0;
    }

    try {
        wallflow::win::App app(instance);
        return app.run();
    } catch (const std::exception& e) {
        show_error(e.what());
        return 1;
    }
}
