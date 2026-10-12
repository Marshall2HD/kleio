#include "pch.h"
#include "HistoryItemRow.xaml.h"
#if __has_include("HistoryItemRow.g.cpp")
#include "HistoryItemRow.g.cpp"
#endif

#include "HistoryItemViewModel.h"
#include "PopupImages.h"
#include "PopupUiState.h"
#include "Strings.h"

#include <cmath>

namespace xaml = winrt::Microsoft::UI::Xaml;

namespace winrt::Kleio::implementation
{
    namespace
    {
        constexpr std::size_t kMaxTitleLength = 500;     // Maccy shortens highlighted titles to 500
        constexpr std::size_t kMaxToolTipLength = 5000;
    }

    HistoryItemRow::~HistoryItemRow()
    {
        Unsubscribe();
    }

    void HistoryItemRow::InitializeComponent()
    {
        HistoryItemRowT<HistoryItemRow>::InitializeComponent();

        DataContextChanged({ this, &HistoryItemRow::HandleDataContextChanged });
        PointerMoved({ this, &HistoryItemRow::HandleRowPointerMoved });
        RightTapped({ this, &HistoryItemRow::HandleRowRightTapped });
        RowToolTip().Opened({ this, &HistoryItemRow::HandleToolTipOpened });
        // The ListViewItem container exists only once the row is in the tree.
        Loaded([this](auto&&, auto&&) { ApplyAutomation(); });
    }

    xaml::Automation::Peers::AutomationPeer HistoryItemRow::OnCreateAutomationPeer()
    {
        if (m_mode == Mode::History)
        {
            return nullptr;
        }
        return xaml::Automation::Peers::FrameworkElementAutomationPeer{ *this };
    }

    xaml::DependencyObject HistoryItemRow::AutomationTarget()
    {
        if (m_mode != Mode::History)
        {
            return *this;
        }
        // DataTemplate root -> ListViewItemPresenter -> ListViewItem (a few levels at most).
        xaml::DependencyObject current = *this;
        for (int i = 0; i < 6 && current; ++i)
        {
            current = xaml::Media::VisualTreeHelper::GetParent(current);
            if (auto container = current ? current.try_as<xaml::Controls::ListViewItem>() : nullptr)
            {
                return container;
            }
        }
        return nullptr;
    }

    void HistoryItemRow::ApplyAutomation()
    {
        try
        {
            auto target = AutomationTarget();
            if (!target)
            {
                return;
            }
            // Maccy ListItemView: the row is one element labelled with the full description; its
            // visual parts are hidden (AccessibilityView="Raw" in the XAML).
            xaml::Automation::AutomationProperties::SetName(target, hstring{ m_automationName });
            xaml::Automation::AutomationProperties::SetAcceleratorKey(target, hstring{ m_automationShortcut });
        }
        catch (...)
        {
        }
    }

    void HistoryItemRow::Unsubscribe() noexcept
    {
        try
        {
            if (m_viewModel && m_token != 0)
            {
                winrt::get_self<HistoryItemViewModel>(m_viewModel)->Changed.Remove(m_token);
            }
        }
        catch (...)
        {
        }
        m_token = 0;
    }

    void HistoryItemRow::Bind(winrt::Kleio::HistoryItemViewModel const& viewModel)
    {
        m_mode = Mode::History;
        if (viewModel != m_viewModel)
        {
            Unsubscribe();
            m_viewModel = viewModel;
            if (m_viewModel)
            {
                auto weak = get_weak();
                m_token = winrt::get_self<HistoryItemViewModel>(m_viewModel)->Changed.Add([weak]() {
                    if (auto self = weak.get())
                    {
                        self->Render();
                    }
                });
            }
        }
        Render();
    }

    void HistoryItemRow::ShowStatic(winrt::Kleio::HistoryItemViewModel const& viewModel, int badge)
    {
        Unsubscribe();
        m_mode = Mode::Static;
        m_viewModel = viewModel;
        m_badge = badge;
        Render();
    }

    void HistoryItemRow::ShowFooter(hstring const& title, hstring const& shortcut, hstring const& tooltip)
    {
        Unsubscribe();
        m_mode = Mode::Footer;
        m_viewModel = nullptr;
        m_footerTooltip = std::wstring{ tooltip };

        AppIconImage().Visibility(xaml::Visibility::Collapsed);
        ColorSwatch().Visibility(xaml::Visibility::Collapsed);
        ThumbnailImage().Visibility(xaml::Visibility::Collapsed);
        TitleText().Visibility(xaml::Visibility::Visible);
        TitleText().Inlines().Clear();
        TitleText().Text(title);
        ApplyBadge(0);
        ApplyShortcut(std::wstring{ shortcut });
        RowRoot().Height(30);
        m_automationName = std::wstring{ title };
        m_automationShortcut = std::wstring{ shortcut };
        ApplyAutomation();
        Render();
    }

    void HistoryItemRow::SetFooterSelected(bool selected)
    {
        m_footerSelected = selected;
        if (m_mode == Mode::Footer)
        {
            ApplySelection(selected);
        }
    }

    void HistoryItemRow::Render()
    {
        try
        {
            if (m_mode == Mode::Footer)
            {
                ApplySelection(m_footerSelected);
                return;
            }

            auto& ui = ::Kleio::PopupUiState::Get();
            HistoryItemViewModel* vm = m_viewModel ? winrt::get_self<HistoryItemViewModel>(m_viewModel) : nullptr;
            if (!vm || !vm->Item())
            {
                ApplySelection(false);
                AppIconImage().Visibility(xaml::Visibility::Collapsed);
                ColorSwatch().Visibility(xaml::Visibility::Collapsed);
                ThumbnailImage().Source(nullptr);
                ThumbnailImage().Visibility(xaml::Visibility::Collapsed);
                TitleText().Inlines().Clear();
                TitleText().Text(L"");
                ApplyBadge(0);
                ApplyShortcut({});
                m_automationName.clear();
                m_automationShortcut.clear();
                ApplyAutomation();
                return;
            }

            auto const& item = *vm->Item();
            ApplySelection(m_mode == Mode::History && vm->IsSelected());

            // Application icon (Maccy: showApplicationIcons).
            xaml::Media::Imaging::BitmapImage icon{ nullptr };
            if (ui.showApplicationIcons && !item.application.empty())
            {
                icon = ::Kleio::PopupImages::AppIcon(item.application, ui.scale);
            }
            AppIconImage().Source(icon);
            AppIconImage().Visibility(icon ? xaml::Visibility::Visible : xaml::Visibility::Collapsed);

            double rowHeight = 30.0;
            bool showSwatch = false;
            const bool showImage = vm->IsImage() && m_mode == Mode::History;
            if (showImage)
            {
                const auto size = vm->ThumbnailSize(ui.imageMaxHeight, ui.thumbnailMaxWidth, ui.scale);
                ThumbnailImage().Width(size.Width);
                ThumbnailImage().Height(size.Height);
                ThumbnailImage().Source(vm->Thumbnail(ui.imageMaxHeight, ui.thumbnailMaxWidth, ui.scale));
                ThumbnailImage().Visibility(xaml::Visibility::Visible);
                TitleText().Visibility(xaml::Visibility::Collapsed);
                rowHeight = std::max(30.0, std::ceil(static_cast<double>(size.Height)) + 8.0);
            }
            else
            {
                ThumbnailImage().Source(nullptr);
                ThumbnailImage().Visibility(xaml::Visibility::Collapsed);
                TitleText().Visibility(xaml::Visibility::Visible);

                std::wstring const& title = vm->DisplayTitle();
                if (title.empty() && vm->IsImage())
                {
                    ApplyTitle(::Kleio::Strings::Get(L"Image"), {});
                }
                else
                {
                    static const std::vector<::Kleio::TextRange> noRanges;
                    ApplyTitle(title, m_mode == Mode::History ? vm->Ranges() : noRanges);
                }

                if (ui.showHexColorSwatch && !vm->IsImage())
                {
                    if (auto color = item.HexColor())
                    {
                        const std::uint32_t argb = *color;
                        winrt::Windows::UI::Color value{};
                        value.A = static_cast<uint8_t>((argb >> 24) & 0xFF);
                        value.R = static_cast<uint8_t>((argb >> 16) & 0xFF);
                        value.G = static_cast<uint8_t>((argb >> 8) & 0xFF);
                        value.B = static_cast<uint8_t>(argb & 0xFF);
                        ColorSwatch().Background(xaml::Media::SolidColorBrush{ value });
                        showSwatch = true;
                    }
                }
            }
            ColorSwatch().Visibility(showSwatch ? xaml::Visibility::Visible : xaml::Visibility::Collapsed);
            RowRoot().Height(rowHeight);

            // Selection order badge (Maccy MultipleSelectionListView) / paste stack number.
            int badge = 0;
            if (m_mode == Mode::Static)
            {
                badge = m_badge;
            }
            else if (ui.multiSelect && vm->IsSelected())
            {
                badge = vm->SelectionIndex() + 1;
            }
            ApplyBadge(badge);

            ApplyShortcut(m_mode == Mode::History ? ui.ShortcutHint(vm->ShortcutKey()) : std::wstring{});

            // Maccy HistoryItemDecorator.accessibilityLabel (+ the item's primary shortcut, e.g. Ctrl+1).
            const std::size_t count = m_mode == Mode::History && ui.multiSelect ? ui.selectionCount : 0;
            m_automationName = ::Kleio::PopupText::Truncate(vm->AccessibilityLabel(count), 200);
            m_automationShortcut = m_mode == Mode::History ? ui.ShortcutHint(vm->ShortcutKey(), ::Kleio::Modifiers::None) : std::wstring{};
            ApplyAutomation();
        }
        catch (...)
        {
            OutputDebugStringW(L"Kleio: failed to render a history row\n");
        }
    }

    void HistoryItemRow::ApplySelection(bool selected)
    {
        SelectionFill().Opacity(selected ? 1.0 : 0.0);
        SelectionPill().Visibility(selected ? xaml::Visibility::Visible : xaml::Visibility::Collapsed);
    }

    void HistoryItemRow::ApplyBadge(int badge)
    {
        if (badge > 0)
        {
            SelectionBadgeText().Text(winrt::to_hstring(badge));
            SelectionBadge().Visibility(xaml::Visibility::Visible);
        }
        else
        {
            SelectionBadge().Visibility(xaml::Visibility::Collapsed);
        }
    }

    void HistoryItemRow::ApplyShortcut(std::wstring const& text)
    {
        if (text.empty())
        {
            ShortcutKeycap().Visibility(xaml::Visibility::Collapsed);
        }
        else
        {
            ShortcutText().Text(hstring{ text });
            ShortcutKeycap().Visibility(xaml::Visibility::Visible);
        }
    }

    void HistoryItemRow::ApplyTitle(std::wstring const& fullTitle, std::vector<::Kleio::TextRange> const& ranges)
    {
        auto& ui = ::Kleio::PopupUiState::Get();
        auto textBlock = TitleText();
        const std::size_t length = std::min<std::size_t>(fullTitle.size(), kMaxTitleLength);
        const std::wstring_view title{ fullTitle.data(), length };

        auto inlines = textBlock.Inlines();
        inlines.Clear();
        if (ranges.empty() || ui.highlightMatch == ::Kleio::HighlightMatch::None)
        {
            textBlock.Text(hstring{ title });
            return;
        }

        auto sorted = ranges;
        std::sort(sorted.begin(), sorted.end(), [](auto const& a, auto const& b) { return a.start < b.start; });

        xaml::Media::Brush accent{ nullptr };
        if (ui.highlightMatch == ::Kleio::HighlightMatch::Color)
        {
            accent = AccentProbe().Foreground();
        }

        auto addRun = [&](std::size_t start, std::size_t end, bool highlighted) {
            xaml::Documents::Run run;
            run.Text(hstring{ title.substr(start, end - start) });
            if (highlighted)
            {
                switch (ui.highlightMatch)
                {
                case ::Kleio::HighlightMatch::Bold:
                    run.FontWeight(winrt::Microsoft::UI::Text::FontWeights::Bold());
                    break;
                case ::Kleio::HighlightMatch::Italic:
                    run.FontStyle(winrt::Windows::UI::Text::FontStyle::Italic);
                    break;
                case ::Kleio::HighlightMatch::Underline:
                    run.TextDecorations(winrt::Windows::UI::Text::TextDecorations::Underline);
                    break;
                case ::Kleio::HighlightMatch::Color:
                    if (accent)
                    {
                        run.Foreground(accent);
                    }
                    run.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
                    break;
                default:
                    break;
                }
            }
            inlines.Append(run);
        };

        std::size_t position = 0;
        for (auto const& range : sorted)
        {
            std::size_t start = std::min(range.start, length);
            const std::size_t end = std::min(range.end, length);
            if (start < position)
            {
                start = position;
            }
            if (end <= start)
            {
                continue;
            }
            if (start > position)
            {
                addRun(position, start, false);
            }
            addRun(start, end, true);
            position = end;
        }
        if (position < length)
        {
            addRun(position, length, false);
        }
    }

    void HistoryItemRow::HandleDataContextChanged(xaml::FrameworkElement const&, xaml::DataContextChangedEventArgs const& args)
    {
        if (m_mode != Mode::History)
        {
            return;
        }
        winrt::Kleio::HistoryItemViewModel viewModel{ nullptr };
        if (auto value = args.NewValue())
        {
            viewModel = value.try_as<winrt::Kleio::HistoryItemViewModel>();
        }
        Bind(viewModel);
    }

    void HistoryItemRow::HandleRowPointerMoved(winrt::Windows::Foundation::IInspectable const&,
                                               xaml::Input::PointerRoutedEventArgs const& args)
    {
        if (m_mode == Mode::Footer)
        {
            return;   // the popup listens to footer rows directly
        }
        auto& ui = ::Kleio::PopupUiState::Get();
        if (ui.rowPointerMoved)
        {
            try
            {
                ui.rowPointerMoved(*this, args);
            }
            catch (...)
            {
            }
        }
    }

    void HistoryItemRow::HandleRowRightTapped(winrt::Windows::Foundation::IInspectable const&,
                                              xaml::Input::RightTappedRoutedEventArgs const& args)
    {
        if (m_mode != Mode::History)
        {
            return;
        }
        auto& ui = ::Kleio::PopupUiState::Get();
        if (ui.rowRightTapped)
        {
            try
            {
                ui.rowRightTapped(*this, args);
            }
            catch (...)
            {
            }
        }
    }

    void HistoryItemRow::HandleToolTipOpened(winrt::Windows::Foundation::IInspectable const&, xaml::RoutedEventArgs const&)
    {
        try
        {
            std::wstring text;
            if (m_mode == Mode::Footer)
            {
                text = m_footerTooltip;
            }
            else if (m_viewModel)
            {
                auto* vm = winrt::get_self<HistoryItemViewModel>(m_viewModel);
                if (auto const& item = vm->Item())
                {
                    text = item->PreviewableText();
                    if (text.empty())
                    {
                        text = vm->IsImage() ? ::Kleio::Strings::Get(L"Image") : item->title;
                    }
                }
            }
            if (text.empty())
            {
                text = std::wstring{ TitleText().Text() };
            }

            xaml::Controls::TextBlock content;
            content.Text(hstring{ ::Kleio::PopupText::Truncate(std::move(text), kMaxToolTipLength) });
            content.TextWrapping(xaml::TextWrapping::Wrap);
            RowToolTip().Content(content);
        }
        catch (...)
        {
        }
    }
}
