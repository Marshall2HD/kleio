#include "pch.h"
#include "SettingsHelpers.h"

#include "Platform/AppInfo.h"
#include "Platform/SystemIntegration.h"
#include "Platform/UpdateChecker.h"
#include "Core/Settings.h"

namespace winrt
{
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::UI::Xaml::Controls;
    using namespace Microsoft::UI::Xaml::Controls::Primitives;
    using namespace Microsoft::UI::Xaml::Media::Imaging;
    using namespace Windows::Storage::Streams;
}

namespace Kleio::SettingsUi
{
    std::wstring FormatModifiers(Modifiers modifiers)
    {
        std::wstring text;
        auto append = [&](Modifiers flag, wchar_t const* name) {
            if (HasFlag(modifiers, flag))
            {
                if (!text.empty())
                {
                    text += L"+";
                }
                text += name;
            }
        };
        append(Modifiers::Ctrl, L"Ctrl");
        append(Modifiers::Alt, L"Alt");
        append(Modifiers::Shift, L"Shift");
        append(Modifiers::Win, L"Win");
        return text.empty() ? std::wstring{ L"no modifier" } : text;
    }

    std::wstring FormatBytes(std::uint64_t bytes)
    {
        wchar_t buffer[64]{};
        if (SUCCEEDED(StrFormatByteSizeEx(bytes, SFBS_FLAGS_ROUND_TO_NEAREST_DISPLAYED_DIGIT, buffer, ARRAYSIZE(buffer))))
        {
            return buffer;
        }
        return std::to_wstring(bytes) + L" bytes";
    }

    namespace
    {
        winrt::fire_and_forget LoadPngAsync(winrt::BitmapImage bitmap, std::vector<std::uint8_t> png)
        {
            auto dispatcher = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
            try
            {
                winrt::InMemoryRandomAccessStream stream;
                winrt::DataWriter writer{ stream };
                writer.WriteBytes(winrt::array_view<std::uint8_t const>{ png.data(), static_cast<std::uint32_t>(png.size()) });
                co_await writer.StoreAsync();
                writer.DetachStream();
                stream.Seek(0);
                co_await wil::resume_foreground(dispatcher);
                co_await bitmap.SetSourceAsync(stream);
            }
            catch (...)
            {
                OutputDebugStringW(L"Kleio: failed to decode an application icon\n");
            }
        }
    }

    winrt::BitmapImage BitmapFromPng(std::vector<std::uint8_t> png)
    {
        winrt::BitmapImage bitmap;
        if (!png.empty())
        {
            LoadPngAsync(bitmap, std::move(png));
        }
        return bitmap;
    }

    namespace
    {
        void ShowInfo(winrt::InfoBar const& infoBar, winrt::InfoBarSeverity severity, std::wstring const& title,
                      std::wstring const& message, winrt::ButtonBase const& action)
        {
            infoBar.Severity(severity);
            infoBar.Title(title);
            infoBar.Message(message);
            infoBar.ActionButton(action);
            infoBar.IsOpen(true);
        }

        winrt::HyperlinkButton Link(wchar_t const* text, std::wstring const& uri)
        {
            winrt::HyperlinkButton link;
            link.Content(winrt::box_value(winrt::hstring{ text }));
            link.NavigateUri(winrt::Windows::Foundation::Uri{ uri });
            return link;
        }

        // Sparkle's "Install Update" from the update alert, reported in the same InfoBar.
        void InstallFromInfoBar(winrt::InfoBar infoBar, winrt::Button checkButton, Kleio::UpdateInfo update)
        {
            checkButton.IsEnabled(false);
            ShowInfo(infoBar, winrt::InfoBarSeverity::Informational, L"Installing update",
                     L"Downloading Kleio " + update.version + L". Kleio restarts when the update is installed.", nullptr);
            Kleio::InstallUpdate(update, [infoBar, checkButton, update](std::wstring const& error, std::wstring const& package) {
                try
                {
                    checkButton.IsEnabled(true);
                    winrt::ButtonBase action{ nullptr };
                    if (!package.empty())
                    {
                        winrt::Button open;
                        open.Content(winrt::box_value(L"Open in App Installer"));
                        open.Click([package](auto&&, auto&&) { Kleio::OpenPackageInstaller(package); });
                        action = open;
                    }
                    else
                    {
                        action = Link(L"Download", update.releaseUrl.empty() ? std::wstring{ Kleio::ReleasesUrl } : update.releaseUrl);
                    }
                    ShowInfo(infoBar, winrt::InfoBarSeverity::Error, L"Couldn't install the update", error, action);
                }
                catch (...)
                {
                }
            });
        }
    }

    winrt::fire_and_forget CheckForUpdates(winrt::InfoBar infoBar, winrt::Button button)
    {
        try
        {
            button.IsEnabled(false);
            infoBar.IsOpen(false);
        }
        catch (...)
        {
        }
        // Like Sparkle, a manual check also counts as the last check.
        Kleio::Settings::Instance().LastUpdateCheck(Kleio::UnixNow());
        Kleio::CheckForUpdate([infoBar, button](Kleio::UpdateCheckResult const& result) {
            try
            {
                button.IsEnabled(true);
                switch (result.status)
                {
                case Kleio::UpdateCheckStatus::Available:
                {
                    auto const update = result.update;
                    std::wstring message = L"Kleio " + update.version + L" is available. You have " +
                        std::wstring{ Kleio::SystemIntegration::AppVersion() } + L".";
                    winrt::ButtonBase action{ nullptr };
                    if (!update.packageUrl.empty())
                    {
                        winrt::Button install;
                        install.Content(winrt::box_value(L"Install update"));
                        install.Click([infoBar, button, update](auto&&, auto&&) { InstallFromInfoBar(infoBar, button, update); });
                        action = install;
                    }
                    else
                    {
                        action = Link(L"Download", update.releaseUrl);
                    }
                    ShowInfo(infoBar, winrt::InfoBarSeverity::Success, L"Update available", message, action);
                    infoBar.Content(Link(L"Release notes", update.releaseUrl));
                    break;
                }
                case Kleio::UpdateCheckStatus::UpToDate:
                    infoBar.Content(nullptr);
                    ShowInfo(infoBar, winrt::InfoBarSeverity::Informational, L"You're up to date",
                             L"Kleio " + std::wstring{ Kleio::SystemIntegration::AppVersion() } + L" is the latest version.", nullptr);
                    break;
                case Kleio::UpdateCheckStatus::NoRelease:
                    infoBar.Content(nullptr);
                    ShowInfo(infoBar, winrt::InfoBarSeverity::Warning, L"Couldn't check for updates",
                             L"No published Kleio release was found.", Link(L"Releases", Kleio::ReleasesUrl));
                    break;
                default:
                    infoBar.Content(nullptr);
                    ShowInfo(infoBar, winrt::InfoBarSeverity::Warning, L"Couldn't check for updates",
                             L"Check your internet connection and try again.", nullptr);
                    break;
                }
            }
            catch (...)
            {
            }
        });
        co_return;
    }

    namespace
    {
        // GDI device name ("\\.\DISPLAY1") -> monitor friendly name ("DELL U2720Q").
        std::map<std::wstring, std::wstring> FriendlyMonitorNames()
        {
            std::map<std::wstring, std::wstring> names;
            UINT32 pathCount = 0;
            UINT32 modeCount = 0;
            if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS)
            {
                return names;
            }
            std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
            std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
            if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) != ERROR_SUCCESS)
            {
                return names;
            }
            paths.resize(pathCount);
            for (auto const& path : paths)
            {
                DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
                source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
                source.header.size = sizeof(source);
                source.header.adapterId = path.sourceInfo.adapterId;
                source.header.id = path.sourceInfo.id;
                if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS)
                {
                    continue;
                }
                DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
                target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
                target.header.size = sizeof(target);
                target.header.adapterId = path.targetInfo.adapterId;
                target.header.id = path.targetInfo.id;
                if (DisplayConfigGetDeviceInfo(&target.header) != ERROR_SUCCESS)
                {
                    continue;
                }
                if (target.monitorFriendlyDeviceName[0] != L'\0')
                {
                    names[source.viewGdiDeviceName] = target.monitorFriendlyDeviceName;
                }
            }
            return names;
        }

        BOOL CALLBACK CollectMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM data)
        {
            auto& devices = *reinterpret_cast<std::vector<std::wstring>*>(data);
            MONITORINFOEXW info{};
            info.cbSize = sizeof(info);
            devices.emplace_back(GetMonitorInfoW(monitor, &info) ? info.szDevice : L"");
            return TRUE;
        }
    }

    std::vector<std::wstring> MonitorNames()
    {
        std::vector<std::wstring> devices;
        EnumDisplayMonitors(nullptr, nullptr, CollectMonitor, reinterpret_cast<LPARAM>(&devices));

        std::map<std::wstring, std::wstring> friendly;
        try
        {
            friendly = FriendlyMonitorNames();
        }
        catch (...)
        {
        }

        std::vector<std::wstring> names;
        for (std::size_t i = 0; i < devices.size(); ++i)
        {
            auto const found = friendly.find(devices[i]);
            names.push_back(found != friendly.end() ? found->second : L"Display " + std::to_wstring(i + 1));
        }
        // Disambiguate identical models ("DELL U2720Q (2)").
        for (std::size_t i = 0; i < names.size(); ++i)
        {
            auto const count = std::count(names.begin(), names.end(), names[i]);
            if (count > 1 && names[i].find(L" (") == std::wstring::npos)
            {
                auto const original = names[i];
                for (std::size_t j = 0; j < names.size(); ++j)
                {
                    if (names[j] == original)
                    {
                        names[j] = original + L" (" + std::to_wstring(j + 1) + L")";
                    }
                }
            }
        }
        return names;
    }

    bool EqualsIgnoreCase(std::wstring_view a, std::wstring_view b) noexcept
    {
        return CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
    }

    namespace
    {
        BOOL CALLBACK CollectWindow(HWND hwnd, LPARAM data)
        {
            auto& windows = *reinterpret_cast<std::vector<HWND>*>(data);
            if (!IsWindowVisible(hwnd) || GetWindowTextLengthW(hwnd) == 0 || GetWindow(hwnd, GW_OWNER) != nullptr)
            {
                return TRUE;
            }
            if ((GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0)
            {
                return TRUE;
            }
            DWORD cloaked = 0;
            if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked != 0)
            {
                return TRUE;
            }
            windows.push_back(hwnd);
            return TRUE;
        }
    }

    std::vector<std::wstring> RunningAppPaths()
    {
        std::vector<HWND> windows;
        EnumWindows(CollectWindow, reinterpret_cast<LPARAM>(&windows));

        wchar_t selfPath[MAX_PATH]{};
        GetModuleFileNameW(nullptr, selfPath, ARRAYSIZE(selfPath));

        std::vector<std::pair<std::wstring, std::wstring>> apps; // display name, path
        for (auto hwnd : windows)
        {
            auto path = AppInfo::ProcessPathForWindow(hwnd);
            if (path.empty() || EqualsIgnoreCase(path, selfPath))
            {
                continue;
            }
            auto const duplicate = std::any_of(apps.begin(), apps.end(), [&](auto const& app) { return EqualsIgnoreCase(app.second, path); });
            if (!duplicate)
            {
                apps.emplace_back(AppInfo::DisplayName(path), std::move(path));
            }
        }
        std::sort(apps.begin(), apps.end(), [](auto const& a, auto const& b) {
            return CompareStringEx(LOCALE_NAME_USER_DEFAULT, LINGUISTIC_IGNORECASE, a.first.c_str(), -1, b.first.c_str(), -1, nullptr, nullptr, 0) == CSTR_LESS_THAN;
        });

        std::vector<std::wstring> paths;
        for (auto& app : apps)
        {
            paths.push_back(std::move(app.second));
        }
        return paths;
    }

    void PrepareDialog(winrt::ContentDialog const& dialog, winrt::XamlRoot const& root)
    {
        dialog.XamlRoot(root);
        if (auto style = FindResource(L"DefaultContentDialogStyle").try_as<winrt::Style>())
        {
            dialog.Style(style);
        }
    }

    winrt::Windows::Foundation::IInspectable FindResource(wchar_t const* key)
    {
        try
        {
            auto resources = winrt::Application::Current().Resources();
            // Lookup also searches merged dictionaries; it throws when the key is missing.
            return resources.Lookup(winrt::box_value(winrt::hstring{ key }));
        }
        catch (...)
        {
        }
        return nullptr;
    }
}
