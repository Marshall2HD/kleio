#pragma once

#include <winrt/Windows.Foundation.h>


namespace Kleio
{
    // Replaces Maccy's Sparkle updater: queries the latest GitHub release of Marshall2HD/kleio
    // (https://api.github.com/repos/Marshall2HD/kleio/releases/latest, tag "vX.Y.Z") and compares it
    // with the package version. Resolves to the newer version string ("0.2.0") or nullptr when up to
    // date or on any error.
    winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::Foundation::IReference<winrt::hstring>> CheckForUpdateAsync();
    inline constexpr wchar_t ReleasesUrl[] = L"https://github.com/Marshall2HD/kleio/releases/latest";
}
