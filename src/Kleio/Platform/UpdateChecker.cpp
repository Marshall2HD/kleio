#include "pch.h"
#include "Platform/UpdateChecker.h"
#include "Platform/SystemIntegration.h"

#include "Core/Settings.h"

#include <winrt/Windows.Management.Deployment.h>

namespace Kleio
{
    namespace
    {
        constexpr wchar_t LatestReleaseApi[] = L"https://api.github.com/repos/Marshall2HD/kleio/releases/latest";

#if defined(_M_ARM64)
        constexpr wchar_t Architecture[] = L"arm64";
#else
        constexpr wchar_t Architecture[] = L"x64";
#endif

        void Log(std::wstring_view message)
        {
            std::wstring line{ L"[Kleio] updates: " };
            line.append(message);
            line.push_back(L'\n');
            ::OutputDebugStringW(line.c_str());
        }

        std::wstring Lower(std::wstring text)
        {
            std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) {
                return (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c - L'A' + L'a') : c;
            });
            return text;
        }

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

        // The release asset to install: "Kleio_1.2.3.0_x64.msix" for this architecture, else a
        // bundle (which carries every architecture).
        std::wstring PackageAssetUrl(winrt::Windows::Data::Json::JsonObject const& release)
        {
            using winrt::Windows::Data::Json::JsonValueType;
            if (!release.HasKey(L"assets") || release.GetNamedValue(L"assets").ValueType() != JsonValueType::Array)
            {
                return {};
            }
            std::wstring bundle;
            for (auto const& value : release.GetNamedArray(L"assets"))
            {
                if (value.ValueType() != JsonValueType::Object)
                {
                    continue;
                }
                auto const asset = value.GetObject();
                std::wstring const name = Lower(std::wstring{ asset.GetNamedString(L"name", L"") });
                std::wstring const url{ asset.GetNamedString(L"browser_download_url", L"") };
                if (url.empty())
                {
                    continue;
                }
                if (name.ends_with(L".msix") && name.find(Architecture) != std::wstring::npos)
                {
                    return url;
                }
                if (bundle.empty() && name.ends_with(L".msixbundle"))
                {
                    bundle = url;
                }
            }
            return bundle;
        }

        winrt::Windows::Web::Http::HttpClient MakeClient(wchar_t const* accept)
        {
            winrt::Windows::Web::Http::HttpClient client;
            auto headers = client.DefaultRequestHeaders();
            headers.UserAgent().TryParseAdd(L"Kleio");
            headers.Accept().TryParseAdd(accept);
            return client;
        }
    }

    double UnixNow() noexcept
    {
        using namespace std::chrono;
        return duration_cast<duration<double>>(system_clock::now().time_since_epoch()).count();
    }

    winrt::fire_and_forget CheckForUpdate(std::function<void(UpdateCheckResult const&)> done)
    {
        auto dispatcher = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
        UpdateCheckResult result;
        try
        {
            const std::wstring current = NormalizeVersion(std::wstring{ SystemIntegration::AppVersion() });

            co_await winrt::resume_background();

            auto client = MakeClient(L"application/vnd.github+json");
            const auto response = co_await client.GetAsync(winrt::Windows::Foundation::Uri{ LatestReleaseApi });
            if (response.StatusCode() == winrt::Windows::Web::Http::HttpStatusCode::NotFound)
            {
                // No release yet, or the repository is private: not an error worth a notification.
                result.status = UpdateCheckStatus::NoRelease;
            }
            else if (response.IsSuccessStatusCode())
            {
                const winrt::hstring body = co_await response.Content().ReadAsStringAsync();
                winrt::Windows::Data::Json::JsonObject json{ nullptr };
                if (winrt::Windows::Data::Json::JsonObject::TryParse(body, json) && json)
                {
                    const std::wstring latest = NormalizeVersion(std::wstring{ json.GetNamedString(L"tag_name", L"") });
                    const auto latestParts = ParseVersion(latest);
                    const auto currentParts = ParseVersion(current);
                    if (latestParts.empty())
                    {
                        result.status = UpdateCheckStatus::Failed;
                    }
                    else if (CompareVersions(latestParts, currentParts) > 0)
                    {
                        result.status = UpdateCheckStatus::Available;
                        result.update.version = latest;
                        result.update.releaseUrl = json.GetNamedString(L"html_url", ReleasesUrl);
                        result.update.packageUrl = PackageAssetUrl(json);
                    }
                    else
                    {
                        result.status = UpdateCheckStatus::UpToDate;
                    }
                }
            }
            else
            {
                Log(L"GitHub answered HTTP " + std::to_wstring(static_cast<int>(response.StatusCode())));
            }
        }
        catch (...)
        {
            result = UpdateCheckResult{};
        }
        if (dispatcher)
        {
            co_await wil::resume_foreground(dispatcher);
        }
        try
        {
            if (done)
            {
                done(result);
            }
        }
        catch (...)
        {
        }
    }

    winrt::fire_and_forget InstallUpdate(UpdateInfo update, std::function<void(std::wstring const&, std::wstring const&)> done)
    {
        auto dispatcher = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
        std::wstring error;
        std::wstring package;
        bool installed = false;
        try
        {
            if (update.packageUrl.empty())
            {
                error = L"The release has no installer package for this PC.";
            }
            else
            {
                co_await winrt::resume_background();

                auto client = MakeClient(L"application/octet-stream");
                const auto response = co_await client.GetAsync(winrt::Windows::Foundation::Uri{ update.packageUrl });
                if (!response.IsSuccessStatusCode())
                {
                    error = L"The download failed (HTTP " + std::to_wstring(static_cast<int>(response.StatusCode())) + L").";
                }
                else
                {
                    const auto buffer = co_await response.Content().ReadAsBufferAsync();

                    wchar_t temp[MAX_PATH + 1]{};
                    const DWORD length = ::GetTempPathW(ARRAYSIZE(temp), temp);
                    package = std::wstring{ temp, length } + L"Kleio-" + update.version + L"-" + Architecture +
                        (update.packageUrl.ends_with(L".msixbundle") ? L".msixbundle" : L".msix");
                    {
                        wil::unique_hfile file{ ::CreateFileW(package.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr) };
                        DWORD written = 0;
                        if (!file || !::WriteFile(file.get(), buffer.data(), buffer.Length(), &written, nullptr) || written != buffer.Length())
                        {
                            error = L"Couldn't save the downloaded package.";
                        }
                    }
                    if (error.empty())
                    {
                        wchar_t url[2084]{}; // INTERNET_MAX_URL_LENGTH
                        DWORD urlLength = ARRAYSIZE(url);
                        winrt::check_hresult(::UrlCreateFromPathW(package.c_str(), url, &urlLength, 0));

                        // Windows (Restart Manager) relaunches Kleio after the deployment shuts it down.
                        ::RegisterApplicationRestart(L"--updated", RESTART_NO_CRASH | RESTART_NO_HANG | RESTART_NO_REBOOT);
                        winrt::Windows::Management::Deployment::PackageManager manager;
                        const auto result = co_await manager.AddPackageAsync(
                            winrt::Windows::Foundation::Uri{ url }, nullptr,
                            winrt::Windows::Management::Deployment::DeploymentOptions::ForceApplicationShutdown);
                        if (result.IsRegistered())
                        {
                            installed = true;
                        }
                        else
                        {
                            error = result.ErrorText();
                        }
                    }
                }
            }
        }
        catch (winrt::hresult_error const& e)
        {
            error = e.message();
        }
        catch (...)
        {
            error = L"The update couldn't be installed.";
        }

        if (dispatcher)
        {
            co_await wil::resume_foreground(dispatcher);
        }
        if (installed)
        {
            // Still running: the deployment didn't need to shut Kleio down. Restart into the new version.
            Log(L"installed " + update.version + L"; restarting");
            try
            {
                winrt::Microsoft::Windows::AppLifecycle::AppInstance::Restart(L"--updated");
            }
            catch (...)
            {
            }
            error = L"Kleio " + update.version + L" was installed. Quit and reopen Kleio to use it.";
        }
        ::UnregisterApplicationRestart();
        if (error.empty())
        {
            error = L"The update couldn't be installed.";
        }
        Log(error);
        try
        {
            if (done)
            {
                done(error, package);
            }
        }
        catch (...)
        {
        }
    }

    void OpenPackageInstaller(std::wstring const& package)
    {
        if (!package.empty())
        {
            ::ShellExecuteW(nullptr, L"open", package.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
    }

    // ---- Scheduled checks ----

    UpdateScheduler& UpdateScheduler::Instance()
    {
        static UpdateScheduler instance;
        return instance;
    }

    void UpdateScheduler::Start()
    {
        if (!m_timer)
        {
            auto dispatcher = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
            if (!dispatcher)
            {
                return;
            }
            m_timer = dispatcher.CreateTimer();
            m_timer.IsRepeating(true);
            m_timer.Tick([this](auto&&, auto&&) { Tick(); });
        }
        // First check shortly after startup (as before), then hourly due-checks.
        m_timer.Stop();
        m_timer.Interval(std::chrono::seconds(10));
        m_timer.Start();
    }

    void UpdateScheduler::Stop()
    {
        if (m_timer)
        {
            m_timer.Stop();
        }
    }

    void UpdateScheduler::Tick()
    {
        if (m_timer && m_timer.Interval() != std::chrono::hours(1))
        {
            m_timer.Stop();
            m_timer.Interval(std::chrono::hours(1));
            m_timer.Start();
        }

        auto& settings = Settings::Instance();
        if (!settings.CheckForUpdates() || m_checking)
        {
            return;
        }
        double const now = UnixNow();
        double const last = settings.LastUpdateCheck();
        double const interval = static_cast<double>(UpdateCheckInterval.count());
        if (last <= now && now - last < interval)
        {
            return;
        }

        m_checking = true;
        settings.LastUpdateCheck(now);
        CheckForUpdate([this](UpdateCheckResult const& result) {
            m_checking = false;
            if (result.status != UpdateCheckStatus::Available)
            {
                return;
            }
            if (result.update.version == Settings::Instance().SkippedUpdateVersion())
            {
                return;
            }
            UpdateFound(result.update);
        });
    }
}
