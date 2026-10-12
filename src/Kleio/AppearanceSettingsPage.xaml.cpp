#include "pch.h"
#include "AppearanceSettingsPage.xaml.h"
#if __has_include("AppearanceSettingsPage.g.cpp")
#include "AppearanceSettingsPage.g.cpp"
#endif

#include "Core/Settings.h"
#include "SettingsHelpers.h"
#include "Strings.h"

#include <cmath>

namespace winrt
{
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::UI::Xaml::Controls;
}

namespace
{
    // Keys whose changes (from the popup, tray menu, ...) this page reflects.
    bool IsAppearanceKey(std::wstring_view key) noexcept
    {
        using S = Kleio::Settings;
        for (auto const candidate : { S::kPopupPosition, S::kPopupScreen, S::kPinTo, S::kImageMaxHeight, S::kOpenPreviewAutomatically,
                                      S::kPreviewDelay, S::kHighlightMatch, S::kShowSpecialSymbols, S::kShowTrayIcon, S::kTrayIcon,
                                      S::kShowRecentCopyInTray, S::kShowSearch, S::kSearchVisibility, S::kShowTitle,
                                      S::kShowApplicationIcons, S::kShowHexColorSwatch, S::kShowFooter, S::kWindowPositionX,
                                      S::kWindowPositionY })
        {
            if (key == candidate)
            {
                return true;
            }
        }
        return false;
    }

    std::int32_t RoundedValue(double value) noexcept
    {
        return static_cast<std::int32_t>(std::lround(value));
    }
}

namespace winrt::Kleio::implementation
{
    AppearanceSettingsPage::~AppearanceSettingsPage()
    {
        Unsubscribe();
    }

    void AppearanceSettingsPage::Page_Loaded(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        try
        {
            LoadScreens();
            LoadValues();
            if (!m_settingsToken)
            {
                auto weak = get_weak();
                m_settingsToken = ::Kleio::Settings::Instance().Changed.Add([weak](std::wstring_view key) {
                    if (!IsAppearanceKey(key))
                    {
                        return;
                    }
                    if (auto self = weak.get())
                    {
                        if (!self->m_loading)
                        {
                            self->LoadValues();
                        }
                    }
                });
            }
        }
        catch (...)
        {
        }
    }

    void AppearanceSettingsPage::Page_Unloaded(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        Unsubscribe();
    }

    void AppearanceSettingsPage::Unsubscribe() noexcept
    {
        if (m_settingsToken)
        {
            ::Kleio::Settings::Instance().Changed.Remove(*m_settingsToken);
            m_settingsToken.reset();
        }
    }

    // "Active screen" + one entry per monitor (Settings::PopupScreen: 0 = active, N = N-th monitor).
    void AppearanceSettingsPage::LoadScreens()
    {
        m_monitors = ::Kleio::SettingsUi::MonitorNames();
        m_loading = true;
        try
        {
            auto items = PopupScreenCombo().Items();
            items.Clear();
            items.Append(winrt::box_value(winrt::hstring{ ::Kleio::Strings::Get(L"Appearance_ActiveScreen") }));
            for (auto const& name : m_monitors)
            {
                items.Append(winrt::box_value(winrt::hstring{ name }));
            }
            // A screen that is not connected right now keeps its own entry so the setting is not
            // silently changed.
            auto const screen = ::Kleio::Settings::Instance().PopupScreen();
            if (screen > static_cast<std::int32_t>(m_monitors.size()))
            {
                for (auto index = static_cast<std::int32_t>(m_monitors.size()) + 1; index <= screen; ++index)
                {
                    items.Append(winrt::box_value(winrt::hstring{ ::Kleio::Strings::Format(L"Appearance_DisplayNotConnected", std::to_wstring(index)) }));
                }
            }
        }
        catch (...)
        {
        }
        m_loading = false;
    }

    void AppearanceSettingsPage::LoadValues()
    {
        auto& settings = ::Kleio::Settings::Instance();
        m_loading = true;
        try
        {
            PopupAtCombo().SelectedIndex(static_cast<int32_t>(settings.PopupPosition()));
            auto const screen = settings.PopupScreen();
            auto const screenCount = static_cast<std::int32_t>(PopupScreenCombo().Items().Size());
            PopupScreenCombo().SelectedIndex(screen >= 0 && screen < screenCount ? screen : 0);
            PinToCombo().SelectedIndex(static_cast<int32_t>(settings.PinTo()));
            ImageHeightBox().Value(settings.ImageMaxHeight());
            OpenPreviewToggle().IsOn(settings.OpenPreviewAutomatically());
            PreviewDelayBox().Value(settings.PreviewDelay());
            HighlightCombo().SelectedIndex(static_cast<int32_t>(settings.HighlightMatch()));
            SpecialSymbolsToggle().IsOn(settings.ShowSpecialSymbols());
            ShowTrayIconToggle().IsOn(settings.ShowTrayIcon());
            TrayIconCombo().SelectedIndex(static_cast<int32_t>(settings.TrayIcon()));
            RecentCopyToggle().IsOn(settings.ShowRecentCopyInTray());
            ShowSearchToggle().IsOn(settings.ShowSearch());
            SearchVisibilityCombo().SelectedIndex(static_cast<int32_t>(settings.SearchVisibility()));
            ShowTitleToggle().IsOn(settings.ShowTitle());
            AppIconsToggle().IsOn(settings.ShowApplicationIcons());
            ColorSwatchToggle().IsOn(settings.ShowHexColorSwatch());
            ShowFooterToggle().IsOn(settings.ShowFooter());
            UpdateDependentControls();
        }
        catch (...)
        {
        }
        m_loading = false;
    }

    void AppearanceSettingsPage::UpdateDependentControls()
    {
        auto& settings = ::Kleio::Settings::Instance();
        auto const position = settings.PopupPosition();
        bool const screenRelevant = position == ::Kleio::PopupPosition::ScreenCenter || position == ::Kleio::PopupPosition::LastPosition;
        PopupScreenCombo().Visibility(screenRelevant && m_monitors.size() > 1 ? winrt::Visibility::Visible : winrt::Visibility::Collapsed);

        bool const lastPosition = position == ::Kleio::PopupPosition::LastPosition;
        ResetPositionButton().Visibility(lastPosition ? winrt::Visibility::Visible : winrt::Visibility::Collapsed);
        bool const movedFromDefault = std::abs(settings.WindowPositionX() - 0.5) > 0.0001 || std::abs(settings.WindowPositionY() - 0.2) > 0.0001;
        ResetPositionButton().IsEnabled(movedFromDefault);

        PreviewDelayCard().IsEnabled(settings.OpenPreviewAutomatically());
        TrayIconCombo().IsEnabled(settings.ShowTrayIcon());
        SearchVisibilityCombo().IsEnabled(settings.ShowSearch());
        FooterInfoBar().IsOpen(!settings.ShowFooter());
    }

    void AppearanceSettingsPage::PopupAtCombo_SelectionChanged(winrt::Windows::Foundation::IInspectable const&, winrt::SelectionChangedEventArgs const&)
    {
        if (m_loading)
        {
            return;
        }
        auto const index = PopupAtCombo().SelectedIndex();
        if (index >= 0)
        {
            ::Kleio::Settings::Instance().PopupPosition(static_cast<::Kleio::PopupPosition>(index));
            UpdateDependentControls();
        }
    }

    void AppearanceSettingsPage::PopupScreenCombo_SelectionChanged(winrt::Windows::Foundation::IInspectable const&, winrt::SelectionChangedEventArgs const&)
    {
        if (m_loading)
        {
            return;
        }
        auto const index = PopupScreenCombo().SelectedIndex();
        if (index >= 0)
        {
            ::Kleio::Settings::Instance().PopupScreen(index);
        }
    }

    void AppearanceSettingsPage::ResetPositionButton_Click(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        ::Kleio::Settings::Instance().ResetWindowPosition();
        UpdateDependentControls();
    }

    void AppearanceSettingsPage::PinToCombo_SelectionChanged(winrt::Windows::Foundation::IInspectable const&, winrt::SelectionChangedEventArgs const&)
    {
        if (m_loading)
        {
            return;
        }
        auto const index = PinToCombo().SelectedIndex();
        if (index >= 0)
        {
            ::Kleio::Settings::Instance().PinTo(static_cast<::Kleio::PinsPosition>(index));
        }
    }

    void AppearanceSettingsPage::ImageHeightBox_ValueChanged(winrt::NumberBox const&, winrt::NumberBoxValueChangedEventArgs const& args)
    {
        if (m_loading)
        {
            return;
        }
        auto& settings = ::Kleio::Settings::Instance();
        if (!std::isnan(args.NewValue()))
        {
            settings.ImageMaxHeight(RoundedValue(args.NewValue()));
        }
        if (std::isnan(args.NewValue()) || ImageHeightBox().Value() != settings.ImageMaxHeight())
        {
            LoadValues();
        }
    }

    void AppearanceSettingsPage::OpenPreviewToggle_Toggled(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        if (m_loading)
        {
            return;
        }
        ::Kleio::Settings::Instance().OpenPreviewAutomatically(OpenPreviewToggle().IsOn());
        UpdateDependentControls();
    }

    void AppearanceSettingsPage::PreviewDelayBox_ValueChanged(winrt::NumberBox const&, winrt::NumberBoxValueChangedEventArgs const& args)
    {
        if (m_loading)
        {
            return;
        }
        auto& settings = ::Kleio::Settings::Instance();
        if (!std::isnan(args.NewValue()))
        {
            settings.PreviewDelay(RoundedValue(args.NewValue()));
        }
        if (std::isnan(args.NewValue()) || PreviewDelayBox().Value() != settings.PreviewDelay())
        {
            LoadValues();
        }
    }

    void AppearanceSettingsPage::HighlightCombo_SelectionChanged(winrt::Windows::Foundation::IInspectable const&, winrt::SelectionChangedEventArgs const&)
    {
        if (m_loading)
        {
            return;
        }
        auto const index = HighlightCombo().SelectedIndex();
        if (index >= 0)
        {
            ::Kleio::Settings::Instance().HighlightMatch(static_cast<::Kleio::HighlightMatch>(index));
        }
    }

    void AppearanceSettingsPage::SpecialSymbolsToggle_Toggled(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        if (!m_loading)
        {
            ::Kleio::Settings::Instance().ShowSpecialSymbols(SpecialSymbolsToggle().IsOn());
        }
    }

    void AppearanceSettingsPage::TrayIconCombo_SelectionChanged(winrt::Windows::Foundation::IInspectable const&, winrt::SelectionChangedEventArgs const&)
    {
        if (m_loading)
        {
            return;
        }
        auto const index = TrayIconCombo().SelectedIndex();
        if (index >= 0)
        {
            ::Kleio::Settings::Instance().TrayIcon(static_cast<::Kleio::TrayIconStyle>(index));
        }
    }

    void AppearanceSettingsPage::ShowTrayIconToggle_Toggled(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        if (m_loading)
        {
            return;
        }
        ::Kleio::Settings::Instance().ShowTrayIcon(ShowTrayIconToggle().IsOn());
        UpdateDependentControls();
    }

    void AppearanceSettingsPage::RecentCopyToggle_Toggled(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        if (!m_loading)
        {
            ::Kleio::Settings::Instance().ShowRecentCopyInTray(RecentCopyToggle().IsOn());
        }
    }

    void AppearanceSettingsPage::SearchVisibilityCombo_SelectionChanged(winrt::Windows::Foundation::IInspectable const&, winrt::SelectionChangedEventArgs const&)
    {
        if (m_loading)
        {
            return;
        }
        auto const index = SearchVisibilityCombo().SelectedIndex();
        if (index >= 0)
        {
            ::Kleio::Settings::Instance().SearchVisibility(static_cast<::Kleio::SearchVisibility>(index));
        }
    }

    void AppearanceSettingsPage::ShowSearchToggle_Toggled(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        if (m_loading)
        {
            return;
        }
        ::Kleio::Settings::Instance().ShowSearch(ShowSearchToggle().IsOn());
        UpdateDependentControls();
    }

    void AppearanceSettingsPage::ShowTitleToggle_Toggled(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        if (!m_loading)
        {
            ::Kleio::Settings::Instance().ShowTitle(ShowTitleToggle().IsOn());
        }
    }

    void AppearanceSettingsPage::AppIconsToggle_Toggled(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        if (!m_loading)
        {
            ::Kleio::Settings::Instance().ShowApplicationIcons(AppIconsToggle().IsOn());
        }
    }

    void AppearanceSettingsPage::ColorSwatchToggle_Toggled(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        if (!m_loading)
        {
            ::Kleio::Settings::Instance().ShowHexColorSwatch(ColorSwatchToggle().IsOn());
        }
    }

    void AppearanceSettingsPage::ShowFooterToggle_Toggled(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        if (m_loading)
        {
            return;
        }
        ::Kleio::Settings::Instance().ShowFooter(ShowFooterToggle().IsOn());
        UpdateDependentControls();
    }
}
