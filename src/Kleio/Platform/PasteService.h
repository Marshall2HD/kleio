#pragma once

#include <windows.h>

namespace Kleio
{
    // Simulates the paste keystroke into the app that was focused before the popup opened.
    class PasteService
    {
    public:
        // Call right before showing the popup: remembers GetForegroundWindow() (ignoring Kleio's
        // own windows).
        void RememberForegroundWindow();
        HWND PreviousForegroundWindow() const noexcept { return m_previous; }
        // Brings the remembered window back to the foreground (best effort, AttachThreadInput trick).
        void RestoreForegroundWindow();
        // Releases any held modifier keys, then sends Ctrl+V with SendInput (virtual key 'V' mapped
        // through the current keyboard layout). Must be called after the popup is hidden; waits briefly
        // for focus to settle.
        void Paste();

    private:
        HWND m_previous = nullptr;
    };
}
