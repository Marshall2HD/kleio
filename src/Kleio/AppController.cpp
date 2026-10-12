#include "pch.h"
#include "AppController.h"

#include "Core/ItemAction.h"
#include "Platform/KeyboardHook.h"
#include "Platform/Ocr.h"
#include "Platform/ScriptingPipe.h"
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

        // Maccy's notification body is the item title; images may not have one (yet).
        std::wstring NotificationText(HistoryItem const& item)
        {
            std::wstring text = item.title;
            if (text.empty())
            {
                text = item.IsImage() ? L"Image" : (!item.Files().empty() ? L"Files" : L"Clipboard item");
            }
            if (text.size() > 250)
            {
                text.resize(249);
                text.push_back(L'…');
            }
            return text;
        }

        // Modifier keys held right now (Maccy's Select intent passes .currentModifierFlags). The
        // keyboard state of Kleio's UI thread is stale while another app has focus, so this uses
        // the asynchronous state.
        Modifiers HeldModifiers() noexcept
        {
            auto down = [](int vk) noexcept { return (::GetAsyncKeyState(vk) & 0x8000) != 0; };
            Modifiers result = Modifiers::None;
            if (down(VK_CONTROL)) result |= Modifiers::Ctrl;
            if (down(VK_MENU)) result |= Modifiers::Alt;
            if (down(VK_SHIFT)) result |= Modifiers::Shift;
            return result;
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
            // Must be subscribed before Register() so clicks reach this process.
            auto dispatcher = winrt::DispatcherQueue::GetForCurrentThread();
            winrt::AppNotificationManager::Default().NotificationInvoked(
                [this, dispatcher](auto&&, winrt::AppNotificationActivatedEventArgs const& args) {
                    auto arguments = args.Arguments();
                    dispatcher.TryEnqueue([this, arguments] { OnNotificationInvoked(arguments); });
                });
            winrt::AppNotificationManager::Default().Register();
        }
        catch (...)
        {
            Log(L"App notifications are unavailable");
        }

        UpdateTray();
        ApplyHotKeys();

        try
        {
            m_scripting = std::make_unique<ScriptingPipe::Server>([this](Scripting::Command const& command) { return RunScript(command); });
        }
        catch (...)
        {
            Log(L"Scripting is unavailable");
        }

        auto& updates = UpdateScheduler::Instance();
        updates.UpdateFound.Add([this](UpdateInfo const& update) { ShowUpdateNotification(update); });
        updates.Start();
    }

    void AppController::Shutdown()
    {
        UpdateScheduler::Instance().Stop();
        m_scripting.reset();
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
        CopyItem(*item, plan.removeFormatting);
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
        CopyItem(*m_pasteStack->items.front(), plan.removeFormatting);
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
            CopyItem(*m_pasteStack->items.front(), plan.removeFormatting);
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

        // Maccy's History.add notifies (with the "write" sound) only for new items, not duplicates.
        if (!duplicate && stored)
        {
            Notify(*stored, L"Write");
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
                 key == Settings::kIgnoreOnlyNextEvent || key == Settings::kShowRecentCopyInTray ||
                 key == Settings::kSaveFiles || key == Settings::kSaveImages || key == Settings::kSaveText)
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
        // Maccy's isStatusItemDisabled: dimmed while paused or when nothing would be saved.
        m_tray->SetDisabled(IsRecordingDisabled());

        std::wstring tooltip = L"Kleio";
        if (s.IgnoreEvents())
        {
            tooltip += s.IgnoreOnlyNextEvent() ? L" (ignoring next copy)" : L" (paused)";
        }
        else if (IsRecordingDisabled())
        {
            tooltip += L" (not saving any type)";
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

    bool AppController::IsRecordingDisabled() const
    {
        auto& s = Settings::Instance();
        return s.IgnoreEvents() || (!s.SaveFiles() && !s.SaveImages() && !s.SaveText());
    }

    // ---- Copy feedback (Maccy's Clipboard.copy + Notifier) ----

    void AppController::CopyItem(HistoryItem const& item, bool removeFormatting)
    {
        m_clipboard->Copy(item, removeFormatting);
        Notify(item, L"Knock");
    }

    void AppController::Notify(HistoryItem const& item, wchar_t const* sound)
    {
        auto& s = Settings::Instance();
        // Maccy plays the sound itself (NSSound) next to a silent notification; Kleio keeps it
        // under its own "Play sounds" setting, since Windows has no per-app sound permission to read.
        if (s.PlaySounds())
        {
            SystemIntegration::PlaySoundNamed(sound);
        }
        if (!s.ShowNotifications())
        {
            return;
        }
        try
        {
            auto manager = winrt::AppNotificationManager::Default();
            // Maccy checks the authorization status; here: notifications turned off for Kleio (or
            // all apps) in Windows Settings, or by policy.
            if (manager.Setting() != winrt::AppNotificationSetting::Enabled)
            {
                return;
            }
            auto notification = winrt::AppNotificationBuilder()
                .AddText(NotificationText(item))
                .MuteAudio()
                .BuildNotification();
            // One entry per kind in the notification center instead of one per copy.
            notification.Tag(sound);
            notification.Group(L"history");
            notification.ExpiresOnReboot(true);
            manager.Show(notification);
        }
        catch (...)
        {
            Log(L"Could not show a notification");
        }
    }

    // ---- Notifications ----

    void AppController::OnNotificationInvoked(winrt::Windows::Foundation::Collections::IMap<winrt::hstring, winrt::hstring> const& arguments)
    {
        auto get = [&arguments](wchar_t const* key) -> std::wstring {
            try
            {
                if (arguments && arguments.HasKey(key))
                {
                    return std::wstring{ arguments.Lookup(key) };
                }
            }
            catch (...)
            {
            }
            return {};
        };
        std::wstring const action = get(L"action");
        if (action == L"installUpdate")
        {
            UpdateInfo update{ get(L"version"), get(L"package"), get(L"release") };
            // Only Kleio's own release assets.
            if (!update.packageUrl.starts_with(L"https://github.com/Marshall2HD/kleio/releases/download/"))
            {
                update.packageUrl.clear();
            }
            InstallUpdate(update);
        }
        else if (action == L"skipUpdate")
        {
            Settings::Instance().SkippedUpdateVersion(get(L"version"));
        }
        else if (action == L"openPackage")
        {
            auto const package = get(L"package");
            if (package.ends_with(L".msix") || package.ends_with(L".msixbundle"))
            {
                OpenPackageInstaller(package);
            }
        }
        else if (action == L"showUpdate")
        {
            OpenAbout();
        }
    }

    // ---- Updates ----

    void AppController::ShowUpdateNotification(UpdateInfo const& update)
    {
        // Sparkle's update alert: Install Update / Skip This Version / Remind Me Later (= dismiss).
        try
        {
            winrt::AppNotificationBuilder builder;
            builder.AddArgument(L"action", L"showUpdate");
            builder.AddText(L"A new version of Kleio is available!");
            builder.AddText(L"Kleio " + update.version + L" is now available. You have " +
                            std::wstring{ SystemIntegration::AppVersion() } + L".");
            if (!update.packageUrl.empty())
            {
                builder.AddButton(winrt::AppNotificationButton(L"Install update")
                                      .AddArgument(L"action", L"installUpdate")
                                      .AddArgument(L"version", update.version)
                                      .AddArgument(L"package", update.packageUrl)
                                      .AddArgument(L"release", update.releaseUrl));
            }
            builder.AddButton(winrt::AppNotificationButton(L"Skip this version")
                                  .AddArgument(L"action", L"skipUpdate")
                                  .AddArgument(L"version", update.version));
            if (!update.releaseUrl.empty())
            {
                builder.AddButton(winrt::AppNotificationButton(update.packageUrl.empty() ? L"Download" : L"Release notes")
                                      .SetInvokeUri(winrt::Uri{ update.releaseUrl }));
            }
            auto notification = builder.BuildNotification();
            notification.Tag(L"update");
            notification.Group(L"updates");
            winrt::AppNotificationManager::Default().Show(notification);
        }
        catch (...)
        {
            Log(L"Could not show the update notification");
        }
    }

    void AppController::ShowUpdateFailedNotification(std::wstring const& error, std::wstring const& package, UpdateInfo const& update)
    {
        try
        {
            winrt::AppNotificationBuilder builder;
            builder.AddText(L"Couldn't install Kleio " + update.version);
            builder.AddText(error);
            if (!package.empty())
            {
                builder.AddButton(winrt::AppNotificationButton(L"Open in App Installer")
                                      .AddArgument(L"action", L"openPackage")
                                      .AddArgument(L"package", package));
            }
            builder.AddButton(winrt::AppNotificationButton(L"Download")
                                  .SetInvokeUri(winrt::Uri{ update.releaseUrl.empty() ? std::wstring{ ReleasesUrl } : update.releaseUrl }));
            auto notification = builder.BuildNotification();
            notification.Tag(L"update");
            notification.Group(L"updates");
            winrt::AppNotificationManager::Default().Show(notification);
        }
        catch (...)
        {
            Log(L"Could not show the update failure notification");
        }
    }

    void AppController::InstallUpdate(UpdateInfo const& update)
    {
        try
        {
            auto notification = winrt::AppNotificationBuilder()
                .AddText(L"Installing Kleio " + update.version)
                .AddText(L"Kleio restarts when the update is installed.")
                .MuteAudio()
                .BuildNotification();
            notification.Tag(L"update");
            notification.Group(L"updates");
            winrt::AppNotificationManager::Default().Show(notification);
        }
        catch (...)
        {
        }
        ::Kleio::InstallUpdate(update, [this, update](std::wstring const& error, std::wstring const& package) {
            ShowUpdateFailedNotification(error, package, update);
        });
    }

    // ---- Scripting (Maccy's App Intents) ----

    void AppController::SetSelectedItem(HistoryItemPtr const& item)
    {
        m_selectedItem = item;
    }

    Scripting::Result AppController::RunScript(Scripting::Command const& command)
    {
        using namespace Scripting;
        if (!m_history)
        {
            return Result{ Unavailable, L"Kleio isn't ready yet.", {} };
        }
        // Maccy's AppIntentError.notFound.
        Result const notFound{ NotFound, L"Clipboard item not found", {} };
        // Maccy indexes History.items: pinned and unpinned items in PinTo order, 1-based.
        auto const items = m_history->AllItems();

        switch (command.verb)
        {
        case Verb::Get:
        {
            HistoryItemPtr item;
            if (command.number)
            {
                item = ItemAt(items, *command.number);
            }
            else
            {
                item = m_selectedItem.lock();
                if (item && std::find(items.begin(), items.end(), item) == items.end())
                {
                    item = nullptr;
                }
                if (!item && !m_history->UnpinnedItems().empty())
                {
                    item = m_history->UnpinnedItems().front();
                }
            }
            if (!item)
            {
                return notFound;
            }
            auto payload = Extract(*item, command.format);
            if (!payload)
            {
                return Result{ NotFound, L"The clipboard item has no " + std::wstring{ FormatName(command.format) } + L" content", {} };
            }
            Result result;
            result.payload = std::move(*payload);
            return result;
        }
        case Verb::Select:
        {
            auto item = ItemAt(items, command.number.value_or(1));
            if (!item)
            {
                return notFound;
            }
            Result result;
            const std::string title = ToUtf8(item->title);
            result.payload.bytes.assign(title.begin(), title.end());
            // Paste (when enabled) goes to the app in front, as with Maccy's intent.
            if (!IsPopupVisible())
            {
                m_paste.RememberForegroundWindow();
            }
            Select(item, HeldModifiers());
            return result;
        }
        case Verb::Delete:
        {
            auto item = ItemAt(items, command.number.value_or(1));
            if (!item)
            {
                return notFound;
            }
            m_history->Delete(item);
            return {};
        }
        case Verb::Clear:
        {
            // Maccy's Clear intent asks for confirmation unless the clear alert is suppressed.
            if (!Settings::Instance().SuppressClearAlert() && !command.yes)
            {
                int const answer = ::MessageBoxW(nullptr,
                    L"Are you sure you want to clear the history?\n\nPinned items are kept. You can't undo this action.",
                    L"Kleio", MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2 | MB_SETFOREGROUND | MB_TOPMOST);
                if (answer != IDOK)
                {
                    return Result{ Cancelled, L"Cancelled", {} };
                }
            }
            Clear();
            return {};
        }
        default:
            return Result{ UsageError, L"Unknown command", {} };
        }
    }
}
