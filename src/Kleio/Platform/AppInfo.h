#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Kleio::AppInfo
{
    // Full executable path of the process owning `hwnd` ("" on failure). Uses
    // QueryFullProcessImageNameW with PROCESS_QUERY_LIMITED_INFORMATION. For UWP apps hosted by
    // ApplicationFrameHost.exe, resolves the hosted app's process (child CoreWindow).
    std::wstring ProcessPathForWindow(HWND hwnd);
    std::wstring ForegroundProcessPath();

    // Friendly name: FileDescription from the version resource, else the file name without ".exe".
    std::wstring DisplayName(std::wstring const& exePath);

    // 32x32 (scaled by `scale`) PNG of the executable's icon (SHGetFileInfo/IShellItemImageFactory
    // + WIC), cached per path. Empty vector on failure.
    std::vector<std::uint8_t> IconPng(std::wstring const& exePath, double scale = 1.0);

    // File name component, e.g. "notepad.exe".
    std::wstring FileName(std::wstring const& path);
}
