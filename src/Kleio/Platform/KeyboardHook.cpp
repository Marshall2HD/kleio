#include "pch.h"
#include "Platform/KeyboardHook.h"

#pragma comment(lib, "user32.lib")

namespace Kleio
{
    namespace
    {
        constexpr DWORD VkV = 'V';
        // Unassigned virtual key. Injected while Win is held so that releasing Win does not open Start.
        constexpr WORD DummyKey = 0xE8;

        // Hook state. The hook runs on the UI thread (the thread that installed it), so plain
        // variables are sufficient.
        // Intentionally leaked so that no COM release runs during static destruction.
        winrt::Microsoft::UI::Dispatching::DispatcherQueue& Queue()
        {
            // winrt projected types delete operator new; wrap the handle in a plain struct.
            struct Holder
            {
                winrt::Microsoft::UI::Dispatching::DispatcherQueue queue{ nullptr };
            };
            static auto* holder = new Holder{};
            return holder->queue;
        }
        bool g_leftWinDown = false;
        bool g_rightWinDown = false;
        bool g_swallowingV = false;      // a Win+V "V" key-down was swallowed; swallow repeats and the key-up
        bool g_suppressStart = false;    // Win+V was handled; keep the Win key-up from opening Start
        bool g_pasteVDown = false;       // Ctrl+V down seen; raise PasteDetected on V up
        bool g_pasteInsertDown = false;  // Shift+Insert down seen; raise on Insert up

        bool IsDown(int vk) noexcept
        {
            return (GetAsyncKeyState(vk) & 0x8000) != 0;
        }

        void InjectDummyKey() noexcept
        {
            INPUT inputs[2]{};
            inputs[0].type = INPUT_KEYBOARD;
            inputs[0].ki.wVk = DummyKey;
            inputs[1].type = INPUT_KEYBOARD;
            inputs[1].ki.wVk = DummyKey;
            inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(2, inputs, sizeof(INPUT));
        }

        enum class HookEvent
        {
            WinV,
            Paste,
        };

        void RaiseLater(HookEvent which) noexcept
        {
            auto raise = [which]() {
                try
                {
                    auto& hook = KeyboardHook::Instance();
                    if (which == HookEvent::WinV)
                    {
                        hook.WinV();
                    }
                    else
                    {
                        hook.PasteDetected();
                    }
                }
                catch (...)
                {
                    OutputDebugStringW(L"Kleio: exception in a keyboard hook event handler\n");
                }
            };

            try
            {
                if (auto const& queue = Queue(); queue && queue.TryEnqueue(raise))
                {
                    return;
                }
            }
            catch (...)
            {
            }
            // No dispatcher queue (should not happen): raise synchronously.
            raise();
        }
    }

    KeyboardHook& KeyboardHook::Instance()
    {
        static KeyboardHook instance;
        return instance;
    }

    void KeyboardHook::EnableWinVReplacement(bool enable)
    {
        if (m_replaceWinV == enable)
        {
            return;
        }
        m_replaceWinV = enable;
        if (!enable)
        {
            g_swallowingV = false;
            g_suppressStart = false;
        }
        UpdateHook();
    }

    void KeyboardHook::EnablePasteDetection(bool enable)
    {
        if (m_detectPaste == enable)
        {
            return;
        }
        m_detectPaste = enable;
        g_pasteVDown = false;
        g_pasteInsertDown = false;
        UpdateHook();
    }

    void KeyboardHook::UpdateHook()
    {
        const bool needed = m_replaceWinV || m_detectPaste;
        if (needed && !m_hook)
        {
            try
            {
                Queue() = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
            }
            catch (...)
            {
                Queue() = nullptr;
            }
            g_leftWinDown = IsDown(VK_LWIN);
            g_rightWinDown = IsDown(VK_RWIN);
            m_hook = SetWindowsHookExW(WH_KEYBOARD_LL, &KeyboardHook::HookProc, GetModuleHandleW(nullptr), 0);
            if (!m_hook)
            {
                OutputDebugStringW(L"Kleio: SetWindowsHookExW(WH_KEYBOARD_LL) failed\n");
            }
        }
        else if (!needed && m_hook)
        {
            UnhookWindowsHookEx(m_hook);
            m_hook = nullptr;
            g_swallowingV = false;
            g_suppressStart = false;
            g_pasteVDown = false;
            g_pasteInsertDown = false;
        }
    }

    LRESULT CALLBACK KeyboardHook::HookProc(int code, WPARAM wParam, LPARAM lParam)
    {
        if (code == HC_ACTION)
        {
            try
            {
                auto const* info = reinterpret_cast<KBDLLHOOKSTRUCT const*>(lParam);
                auto const& self = Instance();
                const bool down = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
                const bool up = wParam == WM_KEYUP || wParam == WM_SYSKEYUP;
                const bool injected = (info->flags & LLKHF_INJECTED) != 0;
                const DWORD vk = info->vkCode;

                if (!injected)
                {
                    if (vk == VK_LWIN && (down || up))
                    {
                        g_leftWinDown = down;
                    }
                    else if (vk == VK_RWIN && (down || up))
                    {
                        g_rightWinDown = down;
                    }

                    // ---- Win+V replacement ----
                    if (vk == VkV)
                    {
                        if (down && g_swallowingV)
                        {
                            return 1; // auto-repeat of a swallowed Win+V
                        }
                        if (up && g_swallowingV)
                        {
                            g_swallowingV = false;
                            return 1;
                        }
                        if (down && self.m_replaceWinV)
                        {
                            const bool win = g_leftWinDown || g_rightWinDown || IsDown(VK_LWIN) || IsDown(VK_RWIN);
                            const bool others = IsDown(VK_CONTROL) || IsDown(VK_MENU) || IsDown(VK_SHIFT);
                            if (win && !others)
                            {
                                g_swallowingV = true;
                                g_suppressStart = true;
                                InjectDummyKey(); // while Win is still held: Start stays closed on release
                                RaiseLater(HookEvent::WinV);
                                return 1;
                            }
                        }
                    }
                    else if ((vk == VK_LWIN || vk == VK_RWIN) && up && g_suppressStart)
                    {
                        if (!g_leftWinDown && !g_rightWinDown)
                        {
                            g_suppressStart = false;
                            InjectDummyKey();
                        }
                    }

                    // ---- Paste detection (Ctrl+V / Shift+Insert, raised on key up like Maccy) ----
                    if (self.m_detectPaste)
                    {
                        if (vk == VkV)
                        {
                            if (down)
                            {
                                const bool ctrl = IsDown(VK_CONTROL);
                                const bool others = IsDown(VK_MENU) || IsDown(VK_SHIFT) || IsDown(VK_LWIN) || IsDown(VK_RWIN);
                                if (ctrl && !others)
                                {
                                    g_pasteVDown = true;
                                }
                            }
                            else if (up && g_pasteVDown)
                            {
                                g_pasteVDown = false;
                                RaiseLater(HookEvent::Paste);
                            }
                        }
                        else if (vk == VK_INSERT)
                        {
                            if (down)
                            {
                                const bool shift = IsDown(VK_SHIFT);
                                const bool others = IsDown(VK_CONTROL) || IsDown(VK_MENU) || IsDown(VK_LWIN) || IsDown(VK_RWIN);
                                if (shift && !others)
                                {
                                    g_pasteInsertDown = true;
                                }
                            }
                            else if (up && g_pasteInsertDown)
                            {
                                g_pasteInsertDown = false;
                                RaiseLater(HookEvent::Paste);
                            }
                        }
                    }
                }
            }
            catch (...)
            {
            }
        }
        return CallNextHookEx(nullptr, code, wParam, lParam);
    }
}
