#pragma once

#include "Core/Event.h"
#include "Core/History.h"
#include "Core/HistoryItem.h"
#include "Core/KeyShortcut.h"
#include "Core/Settings.h"
#include "Core/Storage.h"
#include "Platform/ClipboardService.h"
#include "Platform/HotKeyService.h"
#include "Platform/MessageWindow.h"
#include "Platform/PasteService.h"
#include "Platform/TrayIcon.h"
#include "Platform/UpdateChecker.h"

#include <winrt/Kleio.h>

#include <memory>
#include <optional>
#include <vector>

namespace Kleio
{
    // Paste stack (Maccy's PasteStack): several items selected and "pasted" in sequence. After each
    // detected user paste (Ctrl+V) the next item is put on the clipboard.
    struct PasteStack
    {
        std::vector<HistoryItemPtr> items;
        Modifiers modifiers = Modifiers::None;
    };

    // Composition root: owns every service and the windows, and implements the app-level actions
    // the UI invokes (Maccy's AppState + History.select + AppDelegate). UI thread only.
    class AppController
    {
    public:
        static AppController& Current();

        // Called once from App::OnLaunched.
        void Start();
        // Called on Quit (and on WM_ENDSESSION). Clears history if ClearOnQuit, tears down tray/hooks.
        void Shutdown();

        Kleio::Settings& Settings() noexcept { return Settings::Instance(); }
        Kleio::History& History() noexcept { return *m_history; }
        Kleio::Storage& Storage() noexcept { return *m_storage; }
        ClipboardService& Clipboard() noexcept { return *m_clipboard; }
        Kleio::TrayIcon& TrayIcon() noexcept { return *m_tray; }
        Kleio::PasteService& PasteService() noexcept { return m_paste; }

        // ---- Popup ----
        void TogglePopup();
        void ShowPopup();
        void HidePopup();
        bool IsPopupVisible() const;

        // ---- Item actions (Maccy's History.select / startPasteStack) ----
        // Copies `item` and, depending on `modifiers` + settings, pastes into the previously focused
        // app. Modifiers::None is the default action (Enter / click): copy, removing formatting if
        // RemoveFormattingByDefault, then paste if PasteByDefault. Otherwise ActionForModifiers()
        // decides; Unknown does nothing. Hides the popup first.
        void Select(HistoryItemPtr const& item, Modifiers modifiers);
        // Multi-selection Enter: starts a paste stack with `items` (first item copied immediately).
        void StartPasteStack(std::vector<HistoryItemPtr> items, Modifiers modifiers);
        std::optional<PasteStack> const& CurrentPasteStack() const noexcept { return m_pasteStack; }
        void InterruptPasteStack();
        Event<> PasteStackChanged;

        // ---- Footer actions ----
        // Clear / Clear all honour SuppressClearAlert: when not suppressed the popup shows its
        // confirmation dialog first and then calls these.
        void Clear();
        void ClearAll();
        void OpenSettings();                 // brings the Settings window to front (creating it)
        void OpenAbout();                    // Settings window, About page
        void Quit();

        // ---- Pause (Maccy's "ignore events") ----
        void TogglePause();                  // flips Settings::IgnoreEvents
        void IgnoreNextCopy();               // IgnoreEvents + IgnoreOnlyNextEvent

        // Re-applies the global popup hot key / Win+V replacement after settings change. Returns
        // false if the popup shortcut could not be registered.
        bool ApplyHotKeys();
        // Temporarily releases the popup hot key and the Win+V hook (e.g. while the shortcut
        // recorder listens) without changing settings. Resuming re-applies them.
        void SuspendHotKeys(bool suspend);

        // Shown by Settings when RegisterHotKey fails, etc.
        Event<winrt::hstring> HotKeyRegistrationFailed;

        // ---- App notifications (Maccy's Notifier) ----
        // Button/body clicks of Kleio's notifications, from NotificationInvoked or from a launch by
        // a notification (App::HandleActivation).
        void OnNotificationInvoked(winrt::Windows::Foundation::Collections::IMap<winrt::hstring, winrt::hstring> const& arguments);

        // ---- Updates (Maccy's SoftwareUpdater / Sparkle) ----
        // Downloads and installs `update`; reports failures with a notification.
        void InstallUpdate(UpdateInfo const& update);

    private:
        AppController() = default;

        // Maccy's Clipboard.copy: puts the item on the clipboard, then plays the "knock" sound and
        // shows a notification (when enabled).
        void CopyItem(HistoryItem const& item, bool removeFormatting);
        // Maccy's Notifier.notify(body:sound:): sound (PlaySounds) + notification (ShowNotifications).
        void Notify(HistoryItem const& item, wchar_t const* sound);
        void ShowUpdateNotification(UpdateInfo const& update);
        void ShowUpdateFailedNotification(std::wstring const& error, std::wstring const& package, UpdateInfo const& update);
        // True when no copies are being recorded: paused, or every Save type is off (Maccy's
        // isStatusItemDisabled).
        bool IsRecordingDisabled() const;

        void OnNewCopy(HistoryItemPtr item);
        void OnPasteDetected();
        void OnSettingChanged(std::wstring_view key);
        void UpdateTray();

        std::unique_ptr<MessageWindow> m_messageWindow;
        std::unique_ptr<Kleio::Storage> m_storage;
        std::unique_ptr<Kleio::History> m_history;
        std::unique_ptr<ClipboardService> m_clipboard;
        std::unique_ptr<HotKeyService> m_hotKeys;
        std::unique_ptr<Kleio::TrayIcon> m_tray;
        Kleio::PasteService m_paste;
        std::optional<PasteStack> m_pasteStack;
        bool m_hotKeysSuspended = false;

        winrt::Kleio::PopupWindow m_popup{ nullptr };
        winrt::Kleio::SettingsWindow m_settingsWindow{ nullptr };
    };
}
