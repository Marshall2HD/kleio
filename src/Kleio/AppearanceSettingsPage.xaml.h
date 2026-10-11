#pragma once

#include "AppearanceSettingsPage.g.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace winrt::Kleio::implementation
{
    struct AppearanceSettingsPage : AppearanceSettingsPageT<AppearanceSettingsPage>
    {
        AppearanceSettingsPage() = default;
        ~AppearanceSettingsPage();

        void Page_Loaded(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void Page_Unloaded(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);

        void PopupAtCombo_SelectionChanged(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void PopupScreenCombo_SelectionChanged(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void ResetPositionButton_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void PinToCombo_SelectionChanged(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void ImageHeightBox_ValueChanged(winrt::Microsoft::UI::Xaml::Controls::NumberBox const&, winrt::Microsoft::UI::Xaml::Controls::NumberBoxValueChangedEventArgs const&);
        void OpenPreviewToggle_Toggled(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void PreviewDelayBox_ValueChanged(winrt::Microsoft::UI::Xaml::Controls::NumberBox const&, winrt::Microsoft::UI::Xaml::Controls::NumberBoxValueChangedEventArgs const&);
        void HighlightCombo_SelectionChanged(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void SpecialSymbolsToggle_Toggled(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void TrayIconCombo_SelectionChanged(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void ShowTrayIconToggle_Toggled(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void RecentCopyToggle_Toggled(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void SearchVisibilityCombo_SelectionChanged(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void ShowSearchToggle_Toggled(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void ShowTitleToggle_Toggled(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void AppIconsToggle_Toggled(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void ColorSwatchToggle_Toggled(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void ShowFooterToggle_Toggled(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);

    private:
        void LoadValues();
        void LoadScreens();
        void UpdateDependentControls();
        void Unsubscribe() noexcept;

        bool m_loading = false;
        std::vector<std::wstring> m_monitors;
        std::optional<std::uint64_t> m_settingsToken;
    };
}

namespace winrt::Kleio::factory_implementation
{
    struct AppearanceSettingsPage : AppearanceSettingsPageT<AppearanceSettingsPage, implementation::AppearanceSettingsPage>
    {
    };
}
