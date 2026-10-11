#include "pch.h"
#include "GeneralSettingsPage.xaml.h"
#if __has_include("GeneralSettingsPage.g.cpp")
#include "GeneralSettingsPage.g.cpp"
#endif

#include "AppController.h"
#include "Core/ItemAction.h"
#include "Core/Settings.h"
#include "Platform/SystemIntegration.h"
#include "SettingsHelpers.h"

namespace winrt
{
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::UI::Xaml::Controls;
}

namespace winrt::Kleio::implementation
{
    GeneralSettingsPage::~GeneralSettingsPage()
    {
        Unsubscribe();
    }

    void GeneralSettingsPage::InitializeComponent()
    {
        GeneralSettingsPageT<GeneralSettingsPage>::InitializeComponent();

        PopupRecorder().RequireModifier(true);
        PinRecorder().RequireModifier(true);
        DeleteRecorder().RequireModifier(true);
        PreviewRecorder().RequireModifier(true);

        auto handler = [weak = get_weak()](winrt::Kleio::ShortcutRecorder const& sender, winrt::Windows::Foundation::IInspectable const& args) {
            if (auto self = weak.get())
            {
                self->OnShortcutChanged(sender, args);
            }
        };
        PopupRecorder().ShortcutChanged(handler);
        PinRecorder().ShortcutChanged(handler);
        DeleteRecorder().ShortcutChanged(handler);
        PreviewRecorder().ShortcutChanged(handler);
    }

    void GeneralSettingsPage::Page_Loaded(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        try
        {
            LoadValues();
            LoadShortcuts();
            LoadLaunchAtLoginAsync();

            if (!m_settingsToken)
            {
                auto weak = get_weak();
                m_settingsToken = ::Kleio::Settings::Instance().Changed.Add([weak](std::wstring_view key) {
                    if (auto self = weak.get())
                    {
                        self->OnSettingChanged(key);
                    }
                });
            }
        }
        catch (...)
        {
        }
    }

    void GeneralSettingsPage::Page_Unloaded(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        Unsubscribe();
    }

    void GeneralSettingsPage::Unsubscribe() noexcept
    {
        if (m_settingsToken)
        {
            ::Kleio::Settings::Instance().Changed.Remove(*m_settingsToken);
            m_settingsToken.reset();
        }
    }

    void GeneralSettingsPage::OnSettingChanged(std::wstring_view key)
    {
        if (m_loading)
        {
            return;
        }
        // Shortcuts are only edited here (and are temporarily cleared while a recorder records),
        // so they are not reloaded from change notifications.
        using S = ::Kleio::Settings;
        if (key == S::kPopupShortcut || key == S::kPinShortcut || key == S::kDeleteShortcut || key == S::kTogglePreviewShortcut)
        {
            return;
        }
        try
        {
            LoadValues();
        }
        catch (...)
        {
        }
    }

    void GeneralSettingsPage::LoadValues()
    {
        auto& settings = ::Kleio::Settings::Instance();
        m_loading = true;
        try
        {
            CheckForUpdatesToggle().IsOn(settings.CheckForUpdates());
            SearchModeCombo().SelectedIndex(static_cast<int32_t>(settings.SearchMode()));
            PasteAutomaticallyToggle().IsOn(settings.PasteByDefault());
            RemoveFormattingToggle().IsOn(settings.RemoveFormattingByDefault());
            ReplaceWinVToggle().IsOn(settings.ReplaceWinV());
            PlaySoundsToggle().IsOn(settings.PlaySounds());
            RefreshModifiers();
        }
        catch (...)
        {
        }
        m_loading = false;
    }

    void GeneralSettingsPage::LoadShortcuts()
    {
        auto& settings = ::Kleio::Settings::Instance();
        if (!PopupRecorder().IsRecording())
        {
            PopupRecorder().Shortcut(winrt::hstring{ settings.PopupShortcut().ToString() });
        }
        PinRecorder().Shortcut(winrt::hstring{ settings.PinShortcut().ToString() });
        DeleteRecorder().Shortcut(winrt::hstring{ settings.DeleteShortcut().ToString() });
        PreviewRecorder().Shortcut(winrt::hstring{ settings.TogglePreviewShortcut().ToString() });
    }

    // Maccy's GeneralSettingsPane.refreshModifiers.
    void GeneralSettingsPage::RefreshModifiers()
    {
        auto& settings = ::Kleio::Settings::Instance();
        bool const paste = settings.PasteByDefault();
        bool const plain = settings.RemoveFormattingByDefault();
        using ::Kleio::ItemAction;
        auto const copyText = ::Kleio::SettingsUi::FormatModifiers(::Kleio::ModifiersForAction(ItemAction::Copy, paste, plain));
        auto const pasteText = ::Kleio::SettingsUi::FormatModifiers(::Kleio::ModifiersForAction(ItemAction::Paste, paste, plain));
        auto const plainText = ::Kleio::SettingsUi::FormatModifiers(::Kleio::ModifiersForAction(ItemAction::PasteWithoutFormatting, paste, plain));

        std::wstring text = L"Customize what happens when you select an item with Enter or a click:\n";
        text += L"• Hold " + copyText + L" (" + copyText + L"+Enter or " + copyText + L"+click) to copy the item.\n";
        text += L"• Hold " + pasteText + L" (" + pasteText + L"+Enter or " + pasteText + L"+click) to copy and paste the item.\n";
        text += L"• Hold " + plainText + L" (" + plainText + L"+Enter or " + plainText + L"+click) to copy, remove formatting and paste the item.";
        ModifiersDescription().Text(text);
    }

    winrt::fire_and_forget GeneralSettingsPage::LoadLaunchAtLoginAsync()
    {
        auto strong = get_strong();
        auto dispatcher = this->DispatcherQueue();
        bool enabled = false;
        try
        {
            enabled = co_await ::Kleio::SystemIntegration::IsLaunchAtLoginEnabled();
        }
        catch (...)
        {
        }
        co_await wil::resume_foreground(dispatcher);
        m_loading = true;
        try
        {
            LaunchAtLoginToggle().IsOn(enabled);
        }
        catch (...)
        {
        }
        m_loading = false;
    }

    winrt::fire_and_forget GeneralSettingsPage::SetLaunchAtLoginAsync(bool enable)
    {
        auto strong = get_strong();
        auto dispatcher = this->DispatcherQueue();
        bool result = !enable;
        try
        {
            LaunchAtLoginToggle().IsEnabled(false);
            result = co_await ::Kleio::SystemIntegration::SetLaunchAtLoginEnabled(enable);
        }
        catch (...)
        {
        }
        co_await wil::resume_foreground(dispatcher);
        m_loading = true;
        try
        {
            LaunchAtLoginToggle().IsEnabled(true);
            LaunchAtLoginToggle().IsOn(result);
            LaunchAtLoginInfoBar().IsOpen(enable && !result);
        }
        catch (...)
        {
        }
        m_loading = false;
    }

    void GeneralSettingsPage::LaunchAtLoginToggle_Toggled(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        if (m_loading)
        {
            return;
        }
        SetLaunchAtLoginAsync(LaunchAtLoginToggle().IsOn());
    }

    void GeneralSettingsPage::CheckForUpdatesToggle_Toggled(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        if (m_loading)
        {
            return;
        }
        ::Kleio::Settings::Instance().CheckForUpdates(CheckForUpdatesToggle().IsOn());
    }

    void GeneralSettingsPage::CheckNowButton_Click(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        ::Kleio::SettingsUi::CheckForUpdates(UpdateInfoBar(), CheckNowButton());
    }

    void GeneralSettingsPage::SearchModeCombo_SelectionChanged(winrt::Windows::Foundation::IInspectable const&, winrt::SelectionChangedEventArgs const&)
    {
        if (m_loading)
        {
            return;
        }
        auto const index = SearchModeCombo().SelectedIndex();
        if (index >= 0)
        {
            ::Kleio::Settings::Instance().SearchMode(static_cast<::Kleio::SearchMode>(index));
        }
    }

    void GeneralSettingsPage::PasteAutomaticallyToggle_Toggled(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        if (m_loading)
        {
            return;
        }
        ::Kleio::Settings::Instance().PasteByDefault(PasteAutomaticallyToggle().IsOn());
        RefreshModifiers();
    }

    void GeneralSettingsPage::RemoveFormattingToggle_Toggled(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        if (m_loading)
        {
            return;
        }
        ::Kleio::Settings::Instance().RemoveFormattingByDefault(RemoveFormattingToggle().IsOn());
        RefreshModifiers();
    }

    void GeneralSettingsPage::ReplaceWinVToggle_Toggled(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        if (m_loading)
        {
            return;
        }
        ::Kleio::Settings::Instance().ReplaceWinV(ReplaceWinVToggle().IsOn());
        ApplyHotKeysAndReport();
    }

    void GeneralSettingsPage::ClipboardSettingsLink_Click(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        ::Kleio::SystemIntegration::OpenWindowsClipboardSettings();
    }

    void GeneralSettingsPage::PlaySoundsToggle_Toggled(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        if (m_loading)
        {
            return;
        }
        ::Kleio::Settings::Instance().PlaySounds(PlaySoundsToggle().IsOn());
    }

    void GeneralSettingsPage::OnShortcutChanged(winrt::Kleio::ShortcutRecorder const& sender, winrt::Windows::Foundation::IInspectable const&)
    {
        try
        {
            auto const text = sender.Shortcut();
            auto const shortcut = ::Kleio::KeyShortcut::Parse(std::wstring_view{ text }).value_or(::Kleio::KeyShortcut{});
            auto& settings = ::Kleio::Settings::Instance();
            if (sender == PopupRecorder())
            {
                settings.PopupShortcut(shortcut);
                ApplyHotKeysAndReport();
            }
            else if (sender == PinRecorder())
            {
                settings.PinShortcut(shortcut);
            }
            else if (sender == DeleteRecorder())
            {
                settings.DeleteShortcut(shortcut);
            }
            else if (sender == PreviewRecorder())
            {
                settings.TogglePreviewShortcut(shortcut);
            }
        }
        catch (...)
        {
        }
    }

    void GeneralSettingsPage::ApplyHotKeysAndReport()
    {
        bool const ok = ::Kleio::AppController::Current().ApplyHotKeys();
        HotKeyInfoBar().IsOpen(!ok);
    }
}
