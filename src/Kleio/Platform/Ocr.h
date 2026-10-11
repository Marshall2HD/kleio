#pragma once

#include <winrt/Windows.Foundation.h>

#include <cstdint>
#include <vector>

namespace Kleio
{
    // Windows.Media.Ocr text recognition for image items (Maccy uses Vision for image titles).
    // Runs off the UI thread; returns "" when OCR is unavailable or nothing was recognised.
    // Lines are joined with '\n'.
    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> RecognizeTextAsync(std::vector<std::uint8_t> png);
}
