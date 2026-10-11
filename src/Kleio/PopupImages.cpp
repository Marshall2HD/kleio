#include "pch.h"
#include "PopupImages.h"

#include "Platform/AppInfo.h"

#include <cmath>
#include <map>
#include <memory>

namespace
{
    using winrt::Microsoft::UI::Xaml::Media::Imaging::BitmapImage;

    winrt::fire_and_forget LoadPngAsync(BitmapImage image, std::shared_ptr<std::vector<std::uint8_t> const> png)
    {
        try
        {
            winrt::Windows::Storage::Streams::InMemoryRandomAccessStream stream;
            winrt::Windows::Storage::Streams::DataWriter writer{ stream };
            std::uint8_t const* first = png->data();
            writer.WriteBytes(winrt::array_view<std::uint8_t const>(first, first + png->size()));
            co_await writer.StoreAsync();
            writer.DetachStream();
            stream.Seek(0);
            // Awaiting WinRT async operations resumes on the calling (UI) apartment.
            co_await image.SetSourceAsync(stream);
        }
        catch (...)
        {
            OutputDebugStringW(L"Kleio: failed to decode PNG for the popup\n");
        }
    }

    struct IconKey
    {
        std::wstring path;
        int scalePercent = 100;
        bool operator<(IconKey const& other) const
        {
            if (scalePercent != other.scalePercent)
            {
                return scalePercent < other.scalePercent;
            }
            return path < other.path;
        }
    };

    // Leaked on purpose: WinRT objects must not be released during static destruction.
    std::map<IconKey, BitmapImage>& IconCache()
    {
        static auto* cache = new std::map<IconKey, BitmapImage>();
        return *cache;
    }

    std::map<std::wstring, std::wstring>& NameCache()
    {
        static auto* cache = new std::map<std::wstring, std::wstring>();
        return *cache;
    }
}

namespace Kleio::PopupImages
{
    PixelSize PngSize(std::vector<std::uint8_t> const& png) noexcept
    {
        // 8 byte signature, then the IHDR chunk: length (4), "IHDR" (4), width (4, BE), height (4, BE).
        static constexpr std::uint8_t signature[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
        if (png.size() < 24 || !std::equal(std::begin(signature), std::end(signature), png.begin()))
        {
            return {};
        }
        if (png[12] != 'I' || png[13] != 'H' || png[14] != 'D' || png[15] != 'R')
        {
            return {};
        }
        auto readBigEndian = [&](std::size_t offset) {
            return (static_cast<std::uint32_t>(png[offset]) << 24) | (static_cast<std::uint32_t>(png[offset + 1]) << 16) |
                   (static_cast<std::uint32_t>(png[offset + 2]) << 8) | static_cast<std::uint32_t>(png[offset + 3]);
        };
        PixelSize size;
        size.width = readBigEndian(16);
        size.height = readBigEndian(20);
        return size;
    }

    BitmapImage FromPng(std::vector<std::uint8_t> const& png, int decodePixelHeight, int decodePixelWidth)
    {
        BitmapImage image;
        if (decodePixelHeight > 0)
        {
            image.DecodePixelHeight(decodePixelHeight);
        }
        else if (decodePixelWidth > 0)
        {
            image.DecodePixelWidth(decodePixelWidth);
        }
        if (!png.empty())
        {
            LoadPngAsync(image, std::make_shared<std::vector<std::uint8_t>>(png));
        }
        return image;
    }

    BitmapImage AppIcon(std::wstring const& exePath, double scale)
    {
        if (exePath.empty())
        {
            return nullptr;
        }

        IconKey key{ exePath, static_cast<int>(std::lround(scale * 100.0)) };
        auto& cache = IconCache();
        if (auto it = cache.find(key); it != cache.end())
        {
            return it->second;
        }

        BitmapImage image{ nullptr };
        try
        {
            auto png = Kleio::AppInfo::IconPng(exePath, scale);
            if (!png.empty())
            {
                image = FromPng(png, static_cast<int>(std::lround(16.0 * scale)));
            }
        }
        catch (...)
        {
            image = nullptr;
        }
        cache.emplace(std::move(key), image);
        return image;
    }

    std::wstring const& AppName(std::wstring const& exePath)
    {
        auto& cache = NameCache();
        if (auto it = cache.find(exePath); it != cache.end())
        {
            return it->second;
        }

        std::wstring name;
        try
        {
            name = Kleio::AppInfo::DisplayName(exePath);
        }
        catch (...)
        {
        }
        if (name.empty())
        {
            name = Kleio::AppInfo::FileName(exePath);
        }
        return cache.emplace(exePath, std::move(name)).first->second;
    }
}
