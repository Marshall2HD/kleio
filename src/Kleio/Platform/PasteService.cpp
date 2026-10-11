#include "pch.h"
#include "Platform/PasteService.h"

#pragma comment(lib, "user32.lib")

namespace Kleio
{
    namespace
    {
        // Unassigned virtual key; injecting it between a modifier's down and up keeps Windows from
        // treating a lone Alt/Win press as "open the menu bar" / "open Start".
        constexpr WORD DummyKey = 0xE8;

        bool IsOwnWindow(HWND hwnd) noexcept
        {
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            return pid == GetCurrentProcessId();
        }

        INPUT KeyInput(WORD vk, bool up) noexcept
        {
            INPUT input{};
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = vk;
            input.ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
            input.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
            switch (vk)
            {
            case VK_RCONTROL:
            case VK_RMENU:
            case VK_LWIN:
            case VK_RWIN:
            case VK_INSERT:
                input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
                break;
            default:
                break;
            }
            return input;
        }

        bool IsDown(int vk) noexcept
        {
            return (GetAsyncKeyState(vk) & 0x8000) != 0;
        }

        bool TrySetForeground(HWND hwnd) noexcept
        {
            SetForegroundWindow(hwnd);
            return GetForegroundWindow() == hwnd;
        }
    }

    void PasteService::RememberForegroundWindow()
    {
        try
        {
            const HWND foreground = GetForegroundWindow();
            if (foreground && !IsOwnWindow(foreground))
            {
                m_previous = foreground;
            }
        }
        catch (...)
        {
        }
    }

    void PasteService::RestoreForegroundWindow()
    {
        try
        {
            const HWND target = m_previous;
            if (!target || !IsWindow(target))
            {
                return;
            }
            if (IsIconic(target))
            {
                ShowWindow(target, SW_RESTORE);
            }
            if (TrySetForeground(target))
            {
                return;
            }

            // A zero mouse input makes this process the one that received the last input event,
            // which lifts the foreground lock.
            INPUT nudge{};
            nudge.type = INPUT_MOUSE;
            SendInput(1, &nudge, sizeof(INPUT));
            if (TrySetForeground(target))
            {
                return;
            }

            // Attach to the input queue of the current foreground thread.
            const HWND foreground = GetForegroundWindow();
            const DWORD foregroundThread = foreground ? GetWindowThreadProcessId(foreground, nullptr) : 0;
            const DWORD currentThread = GetCurrentThreadId();
            if (foregroundThread != 0 && foregroundThread != currentThread)
            {
                if (AttachThreadInput(currentThread, foregroundThread, TRUE))
                {
                    BringWindowToTop(target);
                    SetForegroundWindow(target);
                    AttachThreadInput(currentThread, foregroundThread, FALSE);
                }
                if (GetForegroundWindow() == target)
                {
                    return;
                }
            }

            // Last resort: tap Alt (with a dummy key so the target doesn't enter menu mode).
            INPUT altDown[] = { KeyInput(VK_MENU, false) };
            SendInput(1, altDown, sizeof(INPUT));
            SetForegroundWindow(target);
            INPUT altUp[] = { KeyInput(DummyKey, false), KeyInput(DummyKey, true), KeyInput(VK_MENU, true) };
            SendInput(static_cast<UINT>(std::size(altUp)), altUp, sizeof(INPUT));
        }
        catch (...)
        {
        }
    }

    void PasteService::Paste()
    {
        try
        {
            // Release modifiers the user may still be holding (e.g. Alt from Alt+Enter), otherwise the
            // target would see Ctrl+Alt+V etc.
            std::vector<INPUT> release;
            const bool altOrWinHeld = IsDown(VK_LMENU) || IsDown(VK_RMENU) || IsDown(VK_LWIN) || IsDown(VK_RWIN);
            if (altOrWinHeld)
            {
                release.push_back(KeyInput(DummyKey, false));
                release.push_back(KeyInput(DummyKey, true));
            }
            static constexpr WORD kModifierKeys[] = { VK_LCONTROL, VK_RCONTROL, VK_LMENU, VK_RMENU, VK_LSHIFT, VK_RSHIFT, VK_LWIN, VK_RWIN };
            for (WORD vk : kModifierKeys)
            {
                if (IsDown(vk))
                {
                    release.push_back(KeyInput(vk, true));
                }
            }
            if (!release.empty())
            {
                SendInput(static_cast<UINT>(release.size()), release.data(), sizeof(INPUT));
            }

            // Let focus settle on the target window.
            Sleep(50);

            const HWND foreground = GetForegroundWindow();
            const HKL layout = GetKeyboardLayout(foreground ? GetWindowThreadProcessId(foreground, nullptr) : 0);
            const SHORT scan = VkKeyScanExW(L'v', layout);
            WORD vKey = 'V';
            if (scan != -1 && LOBYTE(scan) != 0)
            {
                vKey = LOBYTE(scan);
            }

            INPUT inputs[] = {
                KeyInput(VK_CONTROL, false),
                KeyInput(vKey, false),
                KeyInput(vKey, true),
                KeyInput(VK_CONTROL, true),
            };
            if (SendInput(static_cast<UINT>(std::size(inputs)), inputs, sizeof(INPUT)) != std::size(inputs))
            {
                OutputDebugStringW(L"Kleio: SendInput(Ctrl+V) was blocked\n");
            }
        }
        catch (...)
        {
        }
    }
}
