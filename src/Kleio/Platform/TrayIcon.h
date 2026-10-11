#pragma once

#include "Core/Event.h"
#include "Core/KeyShortcut.h"
#include "Core/Settings.h"
#include "MessageWindow.h"

#include <windows.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Kleio
{
    // Notification-area icon (Shell_NotifyIcon, NOTIFYICON_VERSION_4). Port of Maccy's status item.
    // Icons: TrayIconStyle::Kleio uses Assets\Kleio.ico; the others are rendered at runtime from
    // Segoe Fluent Icons glyphs (Paste E77F, Attach E723, Cut E8C6) in white or black to match the
    // taskbar theme (HKCU ...\Themes\Personalize SystemUsesLightTheme), re-rendered on theme/DPI
    // changes. When paused (ignoreEvents) the icon is drawn at reduced opacity.
    // The icon is re-added when Explorer restarts (TaskbarCreated).
    class TrayIcon
    {
    public:
        struct MenuItem
        {
            std::wstring text;                 // empty text = separator
            std::function<void()> action;
            bool checked = false;
            bool enabled = true;
        };

        explicit TrayIcon(MessageWindow& window);
        ~TrayIcon();

        void Show(TrayIconStyle style);
        void Hide();
        bool IsVisible() const noexcept { return m_visible; }
        void SetStyle(TrayIconStyle style);
        void SetPaused(bool paused);
        void SetTooltip(std::wstring const& text);   // truncated to 127 chars

        // Screen rectangle of the icon (Shell_NotifyIconGetRect), in physical pixels.
        std::optional<RECT> Rect() const;

        // Left click / keyboard select. Argument = modifiers held during the click (Alt+click
        // toggles pause, Alt+Shift+click ignores next copy - handled by the AppController).
        Event<Modifiers> Invoked;
        // Called on right click / context menu key to obtain the menu, which TrayIcon then shows with
        // TrackPopupMenuEx (dark mode aware) and dispatches.
        std::function<std::vector<MenuItem>()> MenuProvider;

    private:
        std::optional<LRESULT> OnMessage(UINT message, WPARAM wParam, LPARAM lParam);
        void ShowContextMenu(int x, int y);
        void RenderIcon();               // (re)creates m_icon for the current style/paused/theme/DPI
        bool Notify(DWORD message);      // Shell_NotifyIconW with the current state
        void Add();                      // NIM_ADD + NIM_SETVERSION

        MessageWindow& m_window;
        std::uint64_t m_handlerToken = 0;
        bool m_visible = false;
        bool m_paused = false;
        TrayIconStyle m_style = TrayIconStyle::Kleio;
        std::wstring m_tooltip;
        HICON m_icon = nullptr;
        UINT m_taskbarCreatedMessage = 0;
        ULONGLONG m_lastInvokeTick = 0;
    };
}
