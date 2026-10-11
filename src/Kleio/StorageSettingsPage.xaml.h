#pragma once

#include "StorageSettingsPage.g.h"

#include <optional>
#include <string_view>

namespace winrt::Kleio::implementation
{
    struct StorageSettingsPage : StorageSettingsPageT<StorageSettingsPage>
    {
        StorageSettingsPage() = default;
        ~StorageSettingsPage();

        void Page_Loaded(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void Page_Unloaded(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void SaveFilesToggle_Toggled(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void SaveImagesToggle_Toggled(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void SaveTextToggle_Toggled(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void HistorySizeBox_ValueChanged(winrt::Microsoft::UI::Xaml::Controls::NumberBox const&, winrt::Microsoft::UI::Xaml::Controls::NumberBoxValueChangedEventArgs const&);
        void SortByCombo_SelectionChanged(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);

    private:
        void LoadValues();
        void UpdateDatabaseSize();
        void Unsubscribe() noexcept;

        bool m_loading = false;
        std::optional<std::uint64_t> m_settingsToken;
    };
}

namespace winrt::Kleio::factory_implementation
{
    struct StorageSettingsPage : StorageSettingsPageT<StorageSettingsPage, implementation::StorageSettingsPage>
    {
    };
}
