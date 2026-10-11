#include "pch.h"
#include "Platform/UpdateChecker.h"
#include "Platform/SystemIntegration.h"

#include <cwctype>

namespace Kleio
{
    namespace
    {
        constexpr wchar_t LatestReleaseApi[] = L"https://api.github.com/repos/Marshall2HD/kleio/releases/latest";

        // "v1.2.3-beta+5" -> "1.2.3"
        std::wstring NormalizeVersion(std::wstring version)
        {
            const auto first = version.find_first_not_of(L" \t\r\n");
            version.erase(0, first == std::wstring::npos ? version.size() : first);
            if (!version.empty() && (version.front() == L'v' || version.front() == L'V'))
            {
                version.erase(0, 1);
            }
            const auto suffix = version.find_first_of(L"-+ \t\r\n");
            if (suffix != std::wstring::npos)
            {
                version.resize(suffix);
            }
            return version;
        }

        std::vector<std::uint64_t> ParseVersion(std::wstring const& version)
        {
            std::vector<std::uint64_t> parts;
            std::uint64_t current = 0;
            bool any = false;
            for (wchar_t c : version)
            {
                if (c == L'.')
                {
                    parts.push_back(current);
                    current = 0;
                    any = false;
                }
                else if (c >= L'0' && c <= L'9')
                {
                    current = current * 10 + static_cast<std::uint64_t>(c - L'0');
                    any = true;
                    if (current > 0xFFFFFFFFull)
                    {
                        return {};
                    }
                }
                else
                {
                    return {}; // not a plain dotted version
                }
            }
            if (any || !parts.empty())
            {
                parts.push_back(current);
            }
            return parts;
        }

        // < 0 if a < b, 0 if equal, > 0 if a > b. Missing components count as 0.
        int CompareVersions(std::vector<std::uint64_t> const& a, std::vector<std::uint64_t> const& b)
        {
            const std::size_t count = (std::max)(a.size(), b.size());
            for (std::size_t i = 0; i < count; ++i)
            {
                const std::uint64_t left = i < a.size() ? a[i] : 0;
                const std::uint64_t right = i < b.size() ? b[i] : 0;
                if (left != right)
                {
                    return left < right ? -1 : 1;
                }
            }
            return 0;
        }
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::Foundation::IReference<winrt::hstring>> CheckForUpdateAsync()
    {
        winrt::Windows::Foundation::IReference<winrt::hstring> result{ nullptr };
        try
        {
            const std::wstring current = NormalizeVersion(std::wstring{ SystemIntegration::AppVersion() });

            co_await winrt::resume_background();

            winrt::Windows::Web::Http::HttpClient client;
            auto headers = client.DefaultRequestHeaders();
            headers.UserAgent().TryParseAdd(L"Kleio");
            headers.Accept().TryParseAdd(L"application/vnd.github+json");

            const auto response = co_await client.GetAsync(winrt::Windows::Foundation::Uri{ LatestReleaseApi });
            if (response.IsSuccessStatusCode())
            {
                const winrt::hstring body = co_await response.Content().ReadAsStringAsync();
                winrt::Windows::Data::Json::JsonObject json{ nullptr };
                if (winrt::Windows::Data::Json::JsonObject::TryParse(body, json) && json)
                {
                    const std::wstring latest = NormalizeVersion(std::wstring{ json.GetNamedString(L"tag_name", L"") });
                    const auto latestParts = ParseVersion(latest);
                    const auto currentParts = ParseVersion(current);
                    if (!latestParts.empty() && CompareVersions(latestParts, currentParts) > 0)
                    {
                        result = winrt::box_value(winrt::hstring{ latest }).as<winrt::Windows::Foundation::IReference<winrt::hstring>>();
                    }
                }
            }
        }
        catch (...)
        {
            result = nullptr;
        }
        co_return result;
    }
}
