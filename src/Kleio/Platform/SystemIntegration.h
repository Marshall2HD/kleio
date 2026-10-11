#pragma once

#include <winrt/Windows.Foundation.h>

namespace Kleio::SystemIntegration
{
    // ---- Launch at login (MSIX StartupTask "KleioStartup") ----
    winrt::Windows::Foundation::IAsyncOperation<bool> IsLaunchAtLoginEnabled();
    // Returns the resulting state (may be false if the user disabled it in Task Manager/Settings).
    winrt::Windows::Foundation::IAsyncOperation<bool> SetLaunchAtLoginEnabled(bool enable);

    // Opens Settings > System > Clipboard (ms-settings:clipboard).
    void OpenWindowsClipboardSettings();

    // ---- Misc ----
    void OpenUri(wchar_t const* uri);
    // Package version "1.2.3" (from Package::Current().Id().Version()).
    winrt::hstring AppVersion();
    // Plays Assets\Sounds\<name>.wav asynchronously (PlaySoundW, SND_ASYNC|SND_FILENAME).
    void PlaySoundNamed(wchar_t const* name);
}
