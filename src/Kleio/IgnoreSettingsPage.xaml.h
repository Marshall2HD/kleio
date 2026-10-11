#pragma once

#include "IgnoreSettingsPage.g.h"

#include <optional>
#include <string>
#include <string_view>

namespace winrt::Kleio::implementation
{
    struct IgnoreSettingsPage : IgnoreSettingsPageT<IgnoreSettingsPage>
    {
        IgnoreSettingsPage() = default;
        ~IgnoreSettingsPage();

        void Page_Loaded(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void Page_Unloaded(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void SectionBar_SelectionChanged(winrt::Microsoft::UI::Xaml::Controls::SelectorBar const&, winrt::Microsoft::UI::Xaml::Controls::SelectorBarSelectionChangedEventArgs const&);

        // Applications
        void IgnoreAllExceptToggle_Toggled(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void RunningAppsFlyout_Opening(winrt::Windows::Foundation::IInspectable const&, winrt::Windows::Foundation::IInspectable const&);
        void AddAppButton_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);

        // Clipboard formats
        void FormatTextBox_KeyDown(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const&);
        void AddFormatButton_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void ResetFormatsButton_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);

        // Regular expressions
        void RegexTextBox_KeyDown(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const&);
        void AddRegexButton_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);

    private:
        void LoadValues();
        void RebuildApps();
        void RebuildFormats();
        void RebuildRegexps();
        void AddApp(std::wstring const& path);
        void RemoveApp(std::wstring const& entry);
        void AddFormat();
        void RemoveFormat(std::wstring const& format);
        void AddRegex();
        void RemoveRegex(std::wstring const& regex);
        winrt::fire_and_forget PickAppAsync();
        winrt::Microsoft::UI::Xaml::Controls::Button MakeRemoveButton(std::wstring const& label, std::function<void()> onRemove);
        void Unsubscribe() noexcept;

        bool m_loading = false;
        std::optional<std::uint64_t> m_settingsToken;
    };
}

namespace winrt::Kleio::factory_implementation
{
    struct IgnoreSettingsPage : IgnoreSettingsPageT<IgnoreSettingsPage, implementation::IgnoreSettingsPage>
    {
    };
}
