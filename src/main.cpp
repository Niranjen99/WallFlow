#include <windows.h>

#include <string>

#include "wallflow/version.hpp"

// Entry point of a Windows GUI program (the wide-character version, because UNICODE is on).
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    const std::wstring text = L"WallFlow " + std::wstring(wallflow::version()) +
                              L"\n\nSkeleton build. The wallpaper window arrives in Step 1.";
    ::MessageBoxW(nullptr, text.c_str(), L"WallFlow", MB_OK | MB_ICONINFORMATION);
    return 0;
}
