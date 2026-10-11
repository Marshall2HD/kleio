#pragma once

#include "Core/Event.h"

#include <windows.h>

namespace Kleio
{
    // Low-level keyboard hook (WH_KEYBOARD_LL), installed on the UI thread, used for two things
    // RegisterHotKey cannot do:
    //  * Replacing Win+V: when enabled, Win+V is swallowed (and the Start menu suppressed on Win key
    //    release by injecting a dummy VK 0xE8 key) and WinV is raised instead, so Kleio opens
    //    instead of the Windows clipboard history.
    //  * Paste detection for the paste stack: PasteDetected is raised when the user presses Ctrl+V
    //    or Shift+Insert anywhere (raised on key up, like Maccy). Injected input (LLKHF_INJECTED,
    //    i.e. Kleio's own paste) is ignored.
    // The hook is only installed while at least one feature is enabled. The callback must stay
    // fast; events are posted to the UI thread's queue rather than raised inside the hook.
    class KeyboardHook
    {
    public:
        static KeyboardHook& Instance();

        void EnableWinVReplacement(bool enable);
        void EnablePasteDetection(bool enable);

        Event<> WinV;
        Event<> PasteDetected;

    private:
        KeyboardHook() = default;
        void UpdateHook();
        static LRESULT CALLBACK HookProc(int code, WPARAM wParam, LPARAM lParam);

        HHOOK m_hook = nullptr;
        bool m_replaceWinV = false;
        bool m_detectPaste = false;
    };
}
