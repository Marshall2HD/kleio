#include "pch.h"
#include "AppController.h"

#include "Core/ItemAction.h"
#include "Platform/KeyboardHook.h"
#include "Platform/Ocr.h"
#include "Platform/SystemIntegration.h"
#include "Platform/UpdateChecker.h"
#include "PopupWindow.xaml.h"
#include "SettingsWindow.xaml.h"

#include <winrt/Microsoft.Windows.AppNotifications.h>
#include <winrt/Microsoft.Windows.AppNotifications.Builder.h>

namespace winrt
{
    using namespace Microsoft::UI::Dispatching;
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::Windows::AppNotifications;
    using namespace Microsoft::Windows::AppNotifications::Builder;
    using namespace Windows::Foundation;
}

namespace Kleio
{
    namespace
    {
        // Delay between a paste and putting the next paste-stack item on the clipboard, so the
        // target app has read the clipboard before it changes.
        constexpr auto PasteStackAdvanceDelay = std::chrono::milliseconds(200);

        void Log(std::wstring_view message)
        {
            std::wstring line{ L"[Kleio] " };
            line.append(message);
            line.push_back(L'\n');
            ::OutputDebugStringW(line.c_str());
        }

        // Maccy's menuIconText: first line of the most recent copy, shortened.
        std::wstring RecentCopyText(HistoryItem const& item)
        {
            std::wstring text = item.Text().value_or(item.title);
            std::erase_if(text, [](wchar_t c) { return c == L'\r' || c == L'\n' || c == L'\t'; });
            auto const first = text.find_first_not_of(L' ');
            text = first == std::wstring::npos ? std::wstring{} : text.substr(first);
            if (text.size() > 60)
            {
                text.resize(59);
                text.push_back(L'…');
            }
            return text;
        }

        winrt::fire_and_forget RecognizeImageTitle(HistoryItemPtr item)
        {
            auto const* png = item->Image();
            if (png == nullptr)
            {
                co_return;
            }
            auto dispatcher = winrt::DispatcherQueue::GetForCurrentThread();
            std::vector<std::uint8_t> bytes = *png;
            winrt::hstring text;
            try
            {
                text = co_await RecognizeTextAsync(std::move(bytes));
            }
            catch (...)
            {
                co_return;
            }
            co_await wil::resume_foreground(dispatcher);
            if (!text.empty() && item->title.empty())
            {
                AppController::Current().History().UpdateTitle(item, std::wstring{ text });
            }
        }

        winrt::fire_and_forget CheckForUpdatesInBackground()
        {
            auto dispatcher = winrt::DispatcherQueue::GetForCurrentThread();
            co_await winrt::resume_after(std::chrono::seconds(10));
            winrt::IReference<winrt::hstring> version{ nullptr };
            try
            {
                version = co_await CheckForUpdateAsync();
            }
            catch (...)
            {
            }
            if (!version)
            {
                co_return;
            }
            co_await wil::resume_foreground(dispatcher);
            try
            {
                auto notification = winrt::AppNotificationBuilder()
                    .AddText(L"Kleio " + version.Value() + L" is available")
                    .AddText(L"Download the new version from GitHub.")
                    .AddButton(winrt::AppNotificationButton(L"Download")
                        .SetInvokeUri(winrt::Uri{ ReleasesUrl }))
                    .BuildNotification();
                winrt::AppNotificationManager::Default().Show(notification);
            }
            catch (...)
            {
                Log(L"Could not show the update notification");
            }
        }
    }

    AppController& AppController::Current()
    {
        static AppController instance;
        return instance;
    }

    void AppController::Start()
    {
        auto& settings = Settings::Instance();

        m_messageWindow = std::make_unique<MessageWindow>();

        try
        {
            m_storage = std::make_unique<Kleio::Storage>(Storage::DefaultPath());
        }
        catch (std::exception const&)
        {
            Log(L"Could not open the history database; history will not be saved");
            m_storage = std::make_unique<Kleio::Storage>(std::filesystem::path{ L":memory:" });
        }
        m_history = std::make_unique<Kleio::History>(*m_storage, settings);
        m_history->Load();

        m_clipboard = std::make_unique<ClipboardService>(*m_messageWindow, settings);
        m_clipboard->NewCopy.Add([this](HistoryItemPtr item) { OnNewCopy(std::move(item)); });
        m_clipboard->ExternalChange.Add([this] { InterruptPasteStack(); });

        m_hotKeys = std::make_unique<HotKeyService>(*m_messageWindow);

        auto& hook = KeyboardHook::Instance();
        hook.ShortcutPressed.Add([this] { HandlePopupShortcut({}); });
        hook.PasteDetected.Add([this] { OnPasteDetected(); });

        m_tray = std::make_unique<Kleio::TrayIcon>(*m_messageWindow);
        m_tray->Invoked.Add([this](Modifiers modifiers) {
            // Clicking the tray icon deactivates (and so auto-hides) an open popup just before the
            // click arrives; don't immediately reopen it.
            if (m_popup && m_popup.WasAutoHiddenWithin(400))
            {
                return;
            }
            // Maccy: Option+click pauses, Option+Shift+click ignores only the next copy.
            if (HasFlag(modifiers, Modifiers::Alt) && HasFlag(modifiers, Modifiers::Shift))
            {
                IgnoreNextCopy();
            }
            else if (HasFlag(modifiers, Modifiers::Alt))
            {
                TogglePause();
            }
            else
            {
                TogglePopup();
            }
        });
        m_tray->MenuProvider = [this] {
            auto& s = Settings::Instance();
            std::wstring open = L"Open Kleio";
            if (auto const shortcut = s.PopupShortcut(); !shortcut.IsEmpty())
            {
                open += L"\t" + shortcut.ToString();
            }
            return std::vector<Kleio::TrayIcon::MenuItem>{
                { open, [this] { ShowPopup(); } },
                { L"", nullptr },
                { L"Pause Kleio", [this] { TogglePause(); }, s.IgnoreEvents() && !s.IgnoreOnlyNextEvent() },
                { L"Ignore next copy", [this] { IgnoreNextCopy(); }, s.IgnoreEvents() && s.IgnoreOnlyNextEvent() },
                { L"", nullptr },
                { L"Settings…", [this] { OpenSettings(); } },
                { L"About Kleio", [this] { OpenAbout(); } },
                { L"", nullptr },
                { L"Quit Kleio", [this] { Quit(); } },
            };
        };

        settings.Changed.Add([this](std::wstring_view key) { OnSettingChanged(key); });
        m_history->Changed.Add([this] { UpdateTray(); });

        try
        {
            winrt::AppNotificationManager::Default().Register();
        }
        catch (...)
        {
            Log(L"App notifications are unavailable");
        }

        UpdateTray();
        ApplyHotKeys();

        if (settings.CheckForUpdates())
        {
            CheckForUpdatesInBackground();
        }
    }

    void AppController::Shutdown()
    {
        if (m_history && Settings::Instance().ClearOnQuit())
        {
            m_history->Clear();
            if (m_clipboard)
            {
                m_clipboard->ClearIfEnabled();   // Maccy's History.clear() -> Clipboard.clear()
            }
        }
        auto& hook = KeyboardHook::Instance();
        hook.SetShortcuts({});
        hook.EnablePasteDetection(false);
        if (m_hotKeys)
        {
            m_hotKeys->Unregister(HotKeyService::PopupHotKeyId);
        }
        if (m_tray)
        {
            m_tray->Hide();
        }
        try
        {
            winrt::AppNotificationManager::Default().Unregister();
        }
        catch (...)
        {
        }
        if (m_popup)
        {
            m_popup.Close();
            m_popup = nullptr;
        }
        if (m_settingsWindow)
        {
            m_settingsWindow.Close();
            m_settingsWindow = nullptr;
        }
    }

    // ---- Popup ----

    void AppController::TogglePopup()
    {
        if (IsPopupVisible())
        {
            HidePopup();
        }
        else
        {
            ShowPopup();
        }
    }

    void AppController::HandlePopupShortcut(KeyShortcut const& shortcut)
    {
        // Maccy Popup.handleFirstKeyDown: open (and watch for cycling), else let the popup decide.
        if (!IsPopupVisible())
        {
            ShowPopup();
            if (m_popup && m_popup.IsShown())
            {
                winrt::get_self<winrt::Kleio::implementation::PopupWindow>(m_popup)->OpenedByShortcut();
            }
            return;
        }
        if (!winrt::get_self<winrt::Kleio::implementation::PopupWindow>(m_popup)->HandleShortcutPressed(shortcut))
        {
            HidePopup();
        }
    }

    void AppController::ShowPopup()
    {
        m_paste.RememberForegroundWindow();
        if (!m_popup)
        {
            m_popup = winrt::make<winrt::Kleio::implementation::PopupWindow>();
        }
        m_popup.ShowPopup();
    }

    void AppController::HidePopup()
    {
        if (m_popup && m_popup.IsShown())
        {
            m_popup.HidePopup();
        }
    }

    bool AppController::IsPopupVisible() const
    {
        return m_popup && m_popup.IsShown();
    }

    // ---- Item actions ----

    namespace
    {
        struct CopyPlan
        {
            bool valid = false;
            bool removeFormatting = false;
            bool paste = false;
        };

        CopyPlan PlanFor(Modifiers modifiers)
        {
            auto& s = Settings::Instance();
            if (modifiers == Modifiers::None)
            {
                return { true, s.RemoveFormattingByDefault(), s.PasteByDefault() };
            }
            switch (ActionForModifiers(modifiers, s.PasteByDefault(), s.RemoveFormattingByDefault()))
            {
            case ItemAction::Copy:
                return { true, false, false };
            case ItemAction::Paste:
                return { true, false, true };
            case ItemAction::PasteWithoutFormatting:
                return { true, true, true };
            default:
                return {};
            }
        }
    }

    void AppController::Select(HistoryItemPtr const& item, Modifiers modifiers)
    {
        if (!item)
        {
            return;
        }
        auto const plan = PlanFor(modifiers);
        if (!plan.valid)
        {
            return;
        }

        HidePopup();
        m_clipboard->Copy(*item, plan.removeFormatting);
        if (Settings::Instance().PlaySounds())
        {
            SystemIntegration::PlaySoundNamed(L"Knock");
        }
        if (plan.paste)
        {
            m_paste.RestoreForegroundWindow();
            m_paste.Paste();
        }
    }

    void AppController::StartPasteStack(std::vector<HistoryItemPtr> items, Modifiers modifiers)
    {
        std::erase(items, nullptr);
        if (items.empty())
        {
            return;
        }
        auto const plan = PlanFor(modifiers);
        if (!plan.valid)
        {
            return;
        }

        m_pasteStack = PasteStack{ std::move(items), modifiers };
        KeyboardHook::Instance().EnablePasteDetection(true);
        PasteStackChanged();

        HidePopup();
        m_clipboard->Copy(*m_pasteStack->items.front(), plan.removeFormatting);
        if (plan.paste)
        {
            m_paste.RestoreForegroundWindow();
            m_paste.Paste();
            // Our own (injected) Ctrl+V is invisible to the paste detector; advance explicitly.
            OnPasteDetected();
        }
    }

    void AppController::OnPasteDetected()
    {
        if (!m_pasteStack)
        {
            return;
        }
        auto dispatcher = winrt::DispatcherQueue::GetForCurrentThread();
        auto timer = dispatcher.CreateTimer();
        timer.Interval(PasteStackAdvanceDelay);
        timer.IsRepeating(false);
        timer.Tick([this](winrt::DispatcherQueueTimer const& sender, auto&&) {
            sender.Stop();
            if (!m_pasteStack || m_pasteStack->items.empty())
            {
                return;
            }
            m_pasteStack->items.erase(m_pasteStack->items.begin());
            if (m_pasteStack->items.empty())
            {
                InterruptPasteStack();
                return;
            }
            auto const plan = PlanFor(m_pasteStack->modifiers);
            m_clipboard->Copy(*m_pasteStack->items.front(), plan.removeFormatting);
            PasteStackChanged();
        });
        timer.Start();
    }

    void AppController::InterruptPasteStack()
    {
        if (!m_pasteStack)
        {
            return;
        }
        m_pasteStack.reset();
        KeyboardHook::Instance().EnablePasteDetection(false);
        PasteStackChanged();
    }

    // ---- Footer actions ----

    void AppController::Clear()
    {
        HidePopup();
        m_history->Clear();
        m_clipboard->ClearIfEnabled();
    }

    void AppController::ClearAll()
    {
        HidePopup();
        m_history->ClearAll();
        m_clipboard->ClearIfEnabled();
    }

    void AppController::OpenSettings()
    {
        HidePopup();
        if (!m_settingsWindow)
        {
            m_settingsWindow = winrt::make<winrt::Kleio::implementation::SettingsWindow>();
            m_settingsWindow.Closed([this](auto&&, auto&&) { m_settingsWindow = nullptr; });
            m_settingsWindow.ShowPage(L"general");
        }
        m_settingsWindow.Activate();
        if (auto native = m_settingsWindow.try_as<::IWindowNative>())
        {
            HWND hwnd = nullptr;
            if (SUCCEEDED(native->get_WindowHandle(&hwnd)) && hwnd != nullptr)
            {
                ::ShowWindow(hwnd, ::IsIconic(hwnd) ? SW_RESTORE : SW_SHOW);
                ::SetForegroundWindow(hwnd);
            }
        }
    }

    void AppController::OpenAbout()
    {
        OpenSettings();
        if (m_settingsWindow)
        {
            m_settingsWindow.ShowPage(L"about");
        }
    }

    void AppController::Quit()
    {
        Shutdown();
        winrt::Application::Current().Exit();
    }

    // ---- Pause ----

    void AppController::TogglePause()
    {
        auto& s = Settings::Instance();
        bool const pause = !s.IgnoreEvents();
        s.IgnoreOnlyNextEvent(false);
        s.IgnoreEvents(pause);
    }

    void AppController::IgnoreNextCopy()
    {
        auto& s = Settings::Instance();
        bool const ignore = !(s.IgnoreEvents() && s.IgnoreOnlyNextEvent());
        s.IgnoreOnlyNextEvent(ignore);
        s.IgnoreEvents(ignore);
    }

    // ---- Hot keys ----

    bool AppController::ApplyHotKeys()
    {
        auto& s = Settings::Instance();
        KeyShortcut const shortcut = s.PopupShortcut();
        KeyShortcut const winV{ Modifiers::Win, 'V' };

        // Explorer keeps several Win shortcuts for itself (Win+V, Win+Shift+C, ...), so RegisterHotKey
        // fails for them; those are served by the keyboard hook instead.
        std::vector<KeyShortcut> hooked;
        if (!m_hotKeysSuspended && s.ReplaceWinV())
        {
            hooked.push_back(winV);
        }

        bool ok = true;
        if (m_hotKeysSuspended || shortcut.IsEmpty())
        {
            m_hotKeys->Unregister(HotKeyService::PopupHotKeyId);
        }
        else if (!m_hotKeys->Register(HotKeyService::PopupHotKeyId, shortcut, [this, shortcut] { HandlePopupShortcut(shortcut); }))
        {
            if (HasFlag(shortcut.modifiers, Modifiers::Win))
            {
                if (shortcut != winV || hooked.empty())
                {
                    hooked.push_back(shortcut);
                }
            }
            else
            {
                ok = false;
                Log(L"Could not register the popup shortcut " + shortcut.ToString());
                HotKeyRegistrationFailed(winrt::hstring{ shortcut.ToString() });
            }
        }
        KeyboardHook::Instance().SetShortcuts(std::move(hooked));
        return ok;
    }

    void AppController::SuspendHotKeys(bool suspend)
    {
        m_hotKeysSuspended = suspend;
        ApplyHotKeys();
    }

    // ---- Events ----

    void AppController::OnNewCopy(HistoryItemPtr item)
    {
        if (!item)
        {
            return;
        }
        bool const needsOcr = item->IsImage() && item->title.empty();
        bool duplicate = false;
        auto token = m_history->ItemAdded.Add([&duplicate](HistoryItemPtr const&, bool wasDuplicate) { duplicate = wasDuplicate; });
        HistoryItemPtr stored = m_history->Add(std::move(item));
        m_history->ItemAdded.Remove(token);

        if (!duplicate && Settings::Instance().PlaySounds())
        {
            SystemIntegration::PlaySoundNamed(L"Write");
        }
        if (stored && needsOcr && stored->title.empty())
        {
            RecognizeImageTitle(stored);
        }
    }

    void AppController::OnSettingChanged(std::wstring_view key)
    {
        if (key == Settings::kPopupShortcut || key == Settings::kReplaceWinV)
        {
            ApplyHotKeys();
        }
        else if (key == Settings::kShowTrayIcon || key == Settings::kTrayIcon || key == Settings::kIgnoreEvents ||
                 key == Settings::kShowRecentCopyInTray)
        {
            UpdateTray();
        }
    }

    void AppController::UpdateTray()
    {
        if (!m_tray)
        {
            return;
        }
        auto& s = Settings::Instance();
        if (!s.ShowTrayIcon())
        {
            m_tray->Hide();
            return;
        }
        if (!m_tray->IsVisible())
        {
            m_tray->Show(s.TrayIcon());
        }
        m_tray->SetStyle(s.TrayIcon());
        m_tray->SetPaused(s.IgnoreEvents());

        std::wstring tooltip = L"Kleio";
        if (s.IgnoreEvents())
        {
            tooltip += s.IgnoreOnlyNextEvent() ? L" (ignoring next copy)" : L" (paused)";
        }
        if (s.ShowRecentCopyInTray() && m_history)
        {
            if (auto recent = m_history->MostRecent())
            {
                if (auto text = RecentCopyText(*recent); !text.empty())
                {
                    tooltip += L"\n" + text;
                }
            }
        }
        m_tray->SetTooltip(tooltip);
    }
}
