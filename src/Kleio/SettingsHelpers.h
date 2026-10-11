#pragma once

#include "Core/KeyShortcut.h"

#include <cstdint>
#include <string>
#include <vector>

// Small helpers shared by the Settings window pages (UI thread only).
namespace Kleio::SettingsUi
{
    // "Ctrl", "Alt+Shift", ... (order Ctrl, Alt, Shift, Win). "no modifier" for None.
    std::wstring FormatModifiers(Modifiers modifiers);

    // "1.2 MB" (StrFormatByteSizeEx).
    std::wstring FormatBytes(std::uint64_t bytes);

    // BitmapImage decoded asynchronously from PNG bytes (empty image when `png` is empty).
    winrt::Microsoft::UI::Xaml::Media::Imaging::BitmapImage BitmapFromPng(std::vector<std::uint8_t> png);

    // Runs the GitHub release check (Platform/UpdateChecker.h) and reports the result in
    // `infoBar`. `button` is disabled while the check runs.
    winrt::fire_and_forget CheckForUpdates(
        winrt::Microsoft::UI::Xaml::Controls::InfoBar infoBar,
        winrt::Microsoft::UI::Xaml::Controls::Button button);

    // Display names of the connected monitors in EnumDisplayMonitors order (the order behind
    // Settings::PopupScreen: N = N-th entry, 1-based). Friendly monitor names when available.
    std::vector<std::wstring> MonitorNames();

    // Distinct executable paths of the processes owning visible top-level windows (excluding
    // Kleio itself), sorted by display name.
    std::vector<std::wstring> RunningAppPaths();

    // Case-insensitive (ordinal) comparison.
    bool EqualsIgnoreCase(std::wstring_view a, std::wstring_view b) noexcept;

    // Applies the ContentDialog default style (needed for dialogs created in code in WinUI 3).
    void PrepareDialog(winrt::Microsoft::UI::Xaml::Controls::ContentDialog const& dialog,
                       winrt::Microsoft::UI::Xaml::XamlRoot const& root);

    // Looks a resource up in the application resources (merged dictionaries included).
    winrt::Windows::Foundation::IInspectable FindResource(wchar_t const* key);
}
