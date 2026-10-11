#include "pch.h"
#include "Platform/Ocr.h"

namespace Kleio
{
    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> RecognizeTextAsync(std::vector<std::uint8_t> png)
    {
        using namespace winrt::Windows::Graphics::Imaging;
        using namespace winrt::Windows::Media::Ocr;
        using namespace winrt::Windows::Storage::Streams;

        std::wstring text;
        try
        {
            co_await winrt::resume_background();

            if (png.empty())
            {
                co_return winrt::hstring{};
            }

            const OcrEngine engine = OcrEngine::TryCreateFromUserProfileLanguages();
            if (!engine)
            {
                co_return winrt::hstring{};
            }

            InMemoryRandomAccessStream stream;
            DataWriter writer{ stream };
            writer.WriteBytes(winrt::array_view<std::uint8_t const>{ png.data(), static_cast<std::uint32_t>(png.size()) });
            co_await writer.StoreAsync();
            writer.DetachStream();
            stream.Seek(0);

            const BitmapDecoder decoder = co_await BitmapDecoder::CreateAsync(stream);
            const std::uint32_t width = decoder.PixelWidth();
            const std::uint32_t height = decoder.PixelHeight();
            const std::uint32_t maxDimension = OcrEngine::MaxImageDimension();
            if (width == 0 || height == 0)
            {
                co_return winrt::hstring{};
            }

            SoftwareBitmap bitmap{ nullptr };
            if (width > maxDimension || height > maxDimension)
            {
                const double scale = (std::min)(static_cast<double>(maxDimension) / width, static_cast<double>(maxDimension) / height);
                BitmapTransform transform;
                transform.ScaledWidth((std::max)(1u, static_cast<std::uint32_t>(width * scale)));
                transform.ScaledHeight((std::max)(1u, static_cast<std::uint32_t>(height * scale)));
                transform.InterpolationMode(BitmapInterpolationMode::Fant);
                bitmap = co_await decoder.GetSoftwareBitmapAsync(BitmapPixelFormat::Bgra8, BitmapAlphaMode::Premultiplied, transform,
                    ExifOrientationMode::RespectExifOrientation, ColorManagementMode::DoNotColorManage);
            }
            else
            {
                bitmap = co_await decoder.GetSoftwareBitmapAsync(BitmapPixelFormat::Bgra8, BitmapAlphaMode::Premultiplied);
            }

            const OcrResult result = co_await engine.RecognizeAsync(bitmap);
            for (auto const& line : result.Lines())
            {
                if (!text.empty())
                {
                    text += L'\n';
                }
                text += std::wstring_view{ line.Text() };
            }
        }
        catch (...)
        {
            text.clear();
        }
        co_return winrt::hstring{ text };
    }
}
