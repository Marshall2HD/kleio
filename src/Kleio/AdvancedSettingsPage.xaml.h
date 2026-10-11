#pragma once

#include "AdvancedSettingsPage.g.h"

#include <optional>

namespace winrt::Kleio::implementation
{
    struct AdvancedSettingsPage : AdvancedSettingsPageT<AdvancedSettingsPage>
    {
        AdvancedSettingsPage() = default;
        ~AdvancedSettingsPage();

        void Page_Loaded(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void Page_Unloaded(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void PauseToggle_Toggled(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void ClearOnQuitToggle_Toggled(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void ClearSystemClipboardToggle_Toggled(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);

    private:
        void LoadValues();
        void Unsubscribe() noexcept;

        bool m_loading = false;
        std::optional<std::uint64_t> m_settingsToken;
    };
}

namespace winrt::Kleio::factory_implementation
{
    struct AdvancedSettingsPage : AdvancedSettingsPageT<AdvancedSettingsPage, implementation::AdvancedSettingsPage>
    {
    };
}
