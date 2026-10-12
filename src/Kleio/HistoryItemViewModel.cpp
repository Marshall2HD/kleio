#include "pch.h"
#include "HistoryItemViewModel.h"
#if __has_include("HistoryItemViewModel.g.cpp")
#include "HistoryItemViewModel.g.cpp"
#endif

#include <cmath>

namespace winrt::Kleio::implementation
{
    HistoryItemViewModel::HistoryItemViewModel(::Kleio::HistoryItemPtr const& item)
    {
        if (item)
        {
            Update(item, {}, {});
        }
    }

    hstring HistoryItemViewModel::Title() const
    {
        if (!m_item)
        {
            return {};
        }
        if (m_title.empty() && m_isImage)
        {
            return L"Image";
        }
        return hstring{ m_title };
    }

    bool HistoryItemViewModel::Update(::Kleio::HistoryItemPtr const& item, std::vector<::Kleio::TextRange> const& ranges,
                                      std::wstring const& shortcutKey)
    {
        if (!item)
        {
            return false;
        }

        bool changed = false;
        if (item != m_item)
        {
            m_item = item;
            m_id = item->id;
            m_pixelSizeKnown = false;
            m_thumbnail = nullptr;
            m_thumbnailDecodeHeight = 0;
            changed = true;
        }

        const bool isImage = item->IsImage();
        if (isImage != m_isImage)
        {
            m_isImage = isImage;
            m_pixelSizeKnown = false;
            m_thumbnail = nullptr;
            m_thumbnailDecodeHeight = 0;
            changed = true;
        }
        if (m_title != item->title)
        {
            m_title = item->title;
            changed = true;
        }
        if (m_pin != item->pin)
        {
            m_pin = item->pin;
            changed = true;
        }
        if (m_application != item->application)
        {
            m_application = item->application;
            changed = true;
        }
        if (m_ranges != ranges)
        {
            m_ranges = ranges;
            changed = true;
        }
        if (m_shortcutKey != shortcutKey)
        {
            m_shortcutKey = shortcutKey;
            changed = true;
        }
        return changed;
    }

    std::wstring HistoryItemViewModel::AccessibilityLabel(std::size_t selectionCount)
    {
        if (!m_item)
        {
            return {};
        }
        std::wstring label;
        if (m_isImage)
        {
            const auto size = ImagePixelSize();
            label = size.IsEmpty() ? std::wstring{ L"Image" }
                                   : L"Image, " + std::to_wstring(size.width) + L"x" + std::to_wstring(size.height);
        }
        else
        {
            label = m_title;
        }
        if (!m_application.empty())
        {
            if (auto const& name = ::Kleio::PopupImages::AppName(m_application); !name.empty())
            {
                label += L", " + name;
            }
        }
        if (IsPinned())
        {
            label += L", Pinned";
        }
        if (selectionCount > 0 && IsSelected())
        {
            label += L", Selected, " + std::to_wstring(m_selectionIndex + 1) + L" of " + std::to_wstring(selectionCount);
        }
        return label;
    }

    std::wstring HistoryItemViewModel::SelectionAnnouncement() const
    {
        if (!m_item)
        {
            return {};
        }
        std::wstring text = m_isImage ? std::wstring{ L"Image" } : m_title;
        if (!m_application.empty())
        {
            if (auto const& name = ::Kleio::PopupImages::AppName(m_application); !name.empty())
            {
                text += L", " + name;
            }
        }
        if (IsPinned())
        {
            text += L", Pinned";
        }
        return text;
    }

    void HistoryItemViewModel::SetSelectionIndex(int index, bool force)
    {
        if (index == m_selectionIndex && !force)
        {
            return;
        }
        m_selectionIndex = index;
        Changed();
    }

    ::Kleio::PopupImages::PixelSize HistoryItemViewModel::ImagePixelSize()
    {
        if (!m_pixelSizeKnown)
        {
            m_pixelSizeKnown = true;
            m_pixelSize = {};
            if (m_item)
            {
                if (auto png = m_item->Image())
                {
                    m_pixelSize = ::Kleio::PopupImages::PngSize(*png);
                }
            }
        }
        return m_pixelSize;
    }

    winrt::Windows::Foundation::Size HistoryItemViewModel::ThumbnailSize(double maxHeight, double maxWidth, double scale)
    {
        maxHeight = std::max(1.0, maxHeight);
        maxWidth = std::max(16.0, maxWidth);
        scale = scale > 0 ? scale : 1.0;

        const auto pixels = ImagePixelSize();
        if (pixels.IsEmpty())
        {
            return { static_cast<float>(maxHeight), static_cast<float>(maxHeight) };
        }

        // One image pixel per physical pixel, limited by the row limits (aspect ratio kept).
        double width = pixels.width / scale;
        double height = pixels.height / scale;
        if (height > maxHeight)
        {
            width *= maxHeight / height;
            height = maxHeight;
        }
        if (width > maxWidth)
        {
            height *= maxWidth / width;
            width = maxWidth;
        }
        return { static_cast<float>(std::max(1.0, width)), static_cast<float>(std::max(1.0, height)) };
    }

    winrt::Microsoft::UI::Xaml::Media::Imaging::BitmapImage HistoryItemViewModel::Thumbnail(double maxHeight, double maxWidth, double scale)
    {
        if (!m_item || !m_isImage)
        {
            return nullptr;
        }
        const auto size = ThumbnailSize(maxHeight, maxWidth, scale);
        const int decodeHeight = std::max(1, static_cast<int>(std::lround(size.Height * (scale > 0 ? scale : 1.0))));
        if (!m_thumbnail || m_thumbnailDecodeHeight != decodeHeight)
        {
            if (auto png = m_item->Image())
            {
                m_thumbnail = ::Kleio::PopupImages::FromPng(*png, decodeHeight);
                m_thumbnailDecodeHeight = decodeHeight;
            }
        }
        return m_thumbnail;
    }

    double HistoryItemViewModel::RowHeight(double imageMaxHeight, double maxWidth, double scale)
    {
        if (!m_isImage)
        {
            return TextRowHeight;
        }
        const auto size = ThumbnailSize(imageMaxHeight, maxWidth, scale);
        return std::max(30.0, std::ceil(static_cast<double>(size.Height)) + 8.0) + 2.0;
    }
}
