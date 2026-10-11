#pragma once

// Image helpers for the popup: PNG bytes -> BitmapImage (decoded asynchronously, once) and a cache
// of application icons. UI thread only.

#include <cstdint>
#include <string>
#include <vector>

namespace Kleio::PopupImages
{
    struct PixelSize
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        bool IsEmpty() const noexcept { return width == 0 || height == 0; }
    };

    // Reads the dimensions from the PNG IHDR chunk (no decoding). Empty on malformed data.
    PixelSize PngSize(std::vector<std::uint8_t> const& png) noexcept;

    // Creates a BitmapImage and starts decoding `png` into it (SetSourceAsync on an
    // InMemoryRandomAccessStream). `decodePixelHeight` / `decodePixelWidth` > 0 limit the decoded size
    // (physical pixels, aspect ratio kept; set at most one of them).
    winrt::Microsoft::UI::Xaml::Media::Imaging::BitmapImage FromPng(std::vector<std::uint8_t> const& png,
                                                                     int decodePixelHeight = 0,
                                                                     int decodePixelWidth = 0);

    // 16x16 DIP icon of an executable (Platform/AppInfo IconPng), cached per path and scale.
    // Returns nullptr when the icon can't be obtained.
    winrt::Microsoft::UI::Xaml::Media::Imaging::BitmapImage AppIcon(std::wstring const& exePath, double scale);

    // Friendly application name (Platform/AppInfo DisplayName), cached per path.
    std::wstring const& AppName(std::wstring const& exePath);
}
