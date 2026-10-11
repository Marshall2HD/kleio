#pragma once

#include "HistoryItemViewModel.g.h"

#include "Core/Event.h"
#include "Core/HistoryItem.h"
#include "Core/Search.h"
#include "PopupImages.h"

#include <string>
#include <vector>

namespace winrt::Kleio::implementation
{
    struct HistoryItemViewModel : HistoryItemViewModelT<HistoryItemViewModel>
    {
        HistoryItemViewModel() = default;
        explicit HistoryItemViewModel(::Kleio::HistoryItemPtr const& item);

        // ---- IDL ----
        int64_t Id() const noexcept { return m_id; }
        hstring Title() const;
        hstring ToString() const { return Title(); }

        // ---- C++ (popup / rows) ----
        ::Kleio::HistoryItemPtr const& Item() const noexcept { return m_item; }

        // Takes the latest state from History / Search. Returns true when something the row shows
        // changed (the caller then raises Changed).
        bool Update(::Kleio::HistoryItemPtr const& item, std::vector<::Kleio::TextRange> const& ranges,
                    std::wstring const& shortcutKey);

        std::wstring const& DisplayTitle() const noexcept { return m_title; }
        std::vector<::Kleio::TextRange> const& Ranges() const noexcept { return m_ranges; }
        // "1".."9" for the first unpinned rows, the pin letter for pinned rows, else empty.
        std::wstring const& ShortcutKey() const noexcept { return m_shortcutKey; }
        bool IsImage() const noexcept { return m_isImage; }
        bool IsPinned() const noexcept { return !m_pin.empty(); }

        // -1 when not selected, otherwise the position in the (multi-)selection.
        int SelectionIndex() const noexcept { return m_selectionIndex; }
        bool IsSelected() const noexcept { return m_selectionIndex >= 0; }
        // Updates the selection index; raises Changed when it changed or `force` is set.
        void SetSelectionIndex(int index, bool force = false);

        // Image items: pixel size of the PNG, the thumbnail size (DIPs) for the given limits and the
        // lazily decoded thumbnail.
        ::Kleio::PopupImages::PixelSize ImagePixelSize();
        winrt::Windows::Foundation::Size ThumbnailSize(double maxHeight, double maxWidth, double scale);
        winrt::Microsoft::UI::Xaml::Media::Imaging::BitmapImage Thumbnail(double maxHeight, double maxWidth, double scale);

        // Height of the row (including its 1 DIP top/bottom margins) in DIPs.
        double RowHeight(double imageMaxHeight, double maxWidth, double scale);

        void NotifyChanged() { Changed(); }

        // Raised when the row showing this item must re-render.
        ::Kleio::Event<> Changed;

        static constexpr double TextRowHeight = 32.0;   // 30 + 2 * 1 margin

    private:
        ::Kleio::HistoryItemPtr m_item;
        std::int64_t m_id = 0;
        std::wstring m_title;
        std::wstring m_pin;
        std::wstring m_application;
        std::vector<::Kleio::TextRange> m_ranges;
        std::wstring m_shortcutKey;
        bool m_isImage = false;
        int m_selectionIndex = -1;

        bool m_pixelSizeKnown = false;
        ::Kleio::PopupImages::PixelSize m_pixelSize;
        winrt::Microsoft::UI::Xaml::Media::Imaging::BitmapImage m_thumbnail{ nullptr };
        int m_thumbnailDecodeHeight = 0;
    };
}

namespace winrt::Kleio::factory_implementation
{
    struct HistoryItemViewModel : HistoryItemViewModelT<HistoryItemViewModel, implementation::HistoryItemViewModel>
    {
    };
}
