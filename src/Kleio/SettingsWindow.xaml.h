#pragma once

#include "SettingsWindow.g.h"

namespace winrt::Kleio::implementation
{
    struct SettingsWindow : SettingsWindowT<SettingsWindow>
    {
        SettingsWindow() = default;

        void InitializeComponent();

        // "general", "storage", "appearance", "ignore", "advanced", "about".
        void ShowPage(winrt::hstring const& tag);

        void NavView_SelectionChanged(
            winrt::Microsoft::UI::Xaml::Controls::NavigationView const& sender,
            winrt::Microsoft::UI::Xaml::Controls::NavigationViewSelectionChangedEventArgs const& args);

    private:
        void SetUpWindow();
        void Navigate(winrt::hstring const& tag);
        winrt::Microsoft::UI::Xaml::Controls::NavigationViewItem FindItem(winrt::hstring const& tag);
        void EnforceMinimumSize();

        winrt::hstring m_currentTag;
        int m_minWidth = 0;   // physical pixels (fallback path only)
        int m_minHeight = 0;
        bool m_useChangedFallback = false;
    };
}

namespace winrt::Kleio::factory_implementation
{
    struct SettingsWindow : SettingsWindowT<SettingsWindow, implementation::SettingsWindow>
    {
    };
}
