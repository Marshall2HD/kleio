#pragma once

#include "HistoryItemRow.g.h"

#include "Core/Event.h"
#include "Core/Search.h"

#include <winrt/Kleio.h>

#include <string>
#include <vector>

namespace winrt::Kleio::implementation
{
    struct HistoryItemRow : HistoryItemRowT<HistoryItemRow>
    {
        enum class Mode
        {
            History,   // history list row, rendered from its DataContext (HistoryItemViewModel)
            Static,    // paste stack entry: no selection / shortcut, numbered badge
            Footer,    // footer entry: title + shortcut text
        };

        HistoryItemRow() = default;
        ~HistoryItemRow();

        void InitializeComponent();

        // History lists: renders `viewModel` and re-renders whenever it raises Changed.
        void Bind(winrt::Kleio::HistoryItemViewModel const& viewModel);
        // Paste stack entry with a numbered badge (`badge` <= 0 hides it).
        void ShowStatic(winrt::Kleio::HistoryItemViewModel const& viewModel, int badge);
        // Footer entry.
        void ShowFooter(hstring const& title, hstring const& shortcut, hstring const& tooltip);
        void SetFooterSelected(bool selected);

        winrt::Kleio::HistoryItemViewModel ViewModel() const noexcept { return m_viewModel; }
        Mode RowMode() const noexcept { return m_mode; }

        // Re-reads the view model and the popup UI state.
        void Render();

        // Footer and paste stack rows are standalone UI Automation elements (named via
        // AutomationProperties.Name); history rows are described by their ListViewItem container.
        winrt::Microsoft::UI::Xaml::Automation::Peers::AutomationPeer OnCreateAutomationPeer();

    private:
        void HandleDataContextChanged(winrt::Microsoft::UI::Xaml::FrameworkElement const& sender,
                                      winrt::Microsoft::UI::Xaml::DataContextChangedEventArgs const& args);
        void HandleRowPointerMoved(winrt::Windows::Foundation::IInspectable const& sender,
                                   winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void HandleRowRightTapped(winrt::Windows::Foundation::IInspectable const& sender,
                                  winrt::Microsoft::UI::Xaml::Input::RightTappedRoutedEventArgs const& args);
        void HandleToolTipOpened(winrt::Windows::Foundation::IInspectable const& sender,
                                 winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);

        void ApplySelection(bool selected);
        void ApplyTitle(std::wstring const& title, std::vector<::Kleio::TextRange> const& ranges);
        void ApplyBadge(int badge);
        void ApplyShortcut(std::wstring const& text);
        void ApplyAutomation();
        winrt::Microsoft::UI::Xaml::DependencyObject AutomationTarget();
        void Unsubscribe() noexcept;

        winrt::Kleio::HistoryItemViewModel m_viewModel{ nullptr };
        ::Kleio::Event<>::Token m_token = 0;
        Mode m_mode = Mode::History;
        int m_badge = 0;
        bool m_footerSelected = false;
        std::wstring m_footerTooltip;
        std::wstring m_automationName;
        std::wstring m_automationShortcut;
    };
}

namespace winrt::Kleio::factory_implementation
{
    struct HistoryItemRow : HistoryItemRowT<HistoryItemRow, implementation::HistoryItemRow>
    {
    };
}
