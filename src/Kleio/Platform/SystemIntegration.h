#pragma once

#include <winrt/Windows.Foundation.h>

namespace Kleio::SystemIntegration
{
    // ---- Launch at login (MSIX StartupTask "KleioStartup") ----
    winrt::Windows::Foundation::IAsyncOperation<bool> IsLaunchAtLoginEnabled();
    // Returns the resulting state (may be false if the user disabled it in Task Manager/Settings).
    winrt::Windows::Foundation::IAsyncOperation<bool> SetLaunchAtLoginEnabled(bool enable);

    // ---- Windows clipboard integration (the "avoid conflicts with Windows" options) ----
    // HKCU\Software\Microsoft\Clipboard\EnableClipboardHistory (DWORD, 1 = on). A missing
    // value means "off", matching Windows.
    bool IsWindowsClipboardHistoryEnabled();
    void SetWindowsClipboardHistoryEnabled(bool enable);
    // HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced\DisabledHotkeys (REG_SZ list
    // of characters). Contains 'V' -> Explorer ignores Win+V after it restarts.
    bool IsWinVDisabledInExplorer();
    void SetWinVDisabledInExplorer(bool disable);
    // Restarts explorer.exe so DisabledHotkeys changes apply (asks nothing; caller confirms).
    void RestartExplorer();
    // Opens Settings > System > Clipboard (ms-settings:clipboard).
    void OpenWindowsClipboardSettings();

    // ---- Misc ----
    void OpenUri(wchar_t const* uri);
    // Package version "1.2.3" (from Package::Current().Id().Version()).
    winrt::hstring AppVersion();
    // Plays Assets\Sounds\<name>.wav asynchronously (PlaySoundW, SND_ASYNC|SND_FILENAME).
    void PlaySoundNamed(wchar_t const* name);
}
