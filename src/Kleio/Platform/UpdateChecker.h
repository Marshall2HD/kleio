#pragma once

#include "Core/Event.h"

#include <winrt/Windows.Foundation.h>

#include <chrono>
#include <functional>
#include <string>

namespace Kleio
{
    // Port of Maccy's SoftwareUpdater (Sparkle). Sparkle reads an appcast; Kleio reads the latest
    // GitHub release of Marshall2HD/kleio (https://api.github.com/repos/Marshall2HD/kleio/releases/latest,
    // tag "vX.Y.Z") and installs its .msix asset for the running architecture.

    struct UpdateInfo
    {
        std::wstring version;      // "1.2.3"
        std::wstring packageUrl;   // browser_download_url of the matching .msix ("" if the release has none)
        std::wstring releaseUrl;   // html_url of the release (release notes)
    };

    enum class UpdateCheckStatus
    {
        UpToDate,
        Available,
        NoRelease,                 // HTTP 404: no published release (or the repository is private)
        Failed,                    // network/parse error
    };

    struct UpdateCheckResult
    {
        UpdateCheckStatus status = UpdateCheckStatus::Failed;
        UpdateInfo update;         // filled when Available
    };

    // Sparkle's default SUScheduledCheckInterval (86400 s); Maccy's Info.plist doesn't override it.
    inline constexpr std::chrono::seconds UpdateCheckInterval{ 24 * 60 * 60 };

    inline constexpr wchar_t ReleasesUrl[] = L"https://github.com/Marshall2HD/kleio/releases/latest";

    // Queries GitHub off the UI thread; `done` runs on the calling (UI) thread. Never throws.
    winrt::fire_and_forget CheckForUpdate(std::function<void(UpdateCheckResult const&)> done);

    // Sparkle's "Install Update": downloads the .msix to %TEMP% and installs it with
    // PackageManager.AddPackageAsync(ForceApplicationShutdown). Kleio registers for restart first
    // (RegisterApplicationRestart "--updated"), so Windows relaunches it after the update shuts it
    // down; if the deployment finishes without shutting Kleio down, it restarts itself through
    // AppInstance::Restart. On failure `done(error, downloadedPackagePath)` runs on the calling thread
    // (path is "" if the download failed) so the caller can offer App Installer as a fallback.
    winrt::fire_and_forget InstallUpdate(UpdateInfo update, std::function<void(std::wstring const& error, std::wstring const& package)> done);

    // Opens a downloaded package with App Installer (the .msix file association).
    void OpenPackageInstaller(std::wstring const& package);

    // Sparkle's scheduled checks: while CheckForUpdates is on, checks when the last check
    // (Settings::LastUpdateCheck, also updated by "Check now") is UpdateCheckInterval old, at
    // startup and from an hourly timer (so sleep/hibernation can't push a check out by a day).
    // UI thread only.
    class UpdateScheduler
    {
    public:
        static UpdateScheduler& Instance();

        void Start();
        void Stop();

        // A scheduled check found a newer release that the user hasn't skipped (Sparkle's
        // SUSkippedVersion). Raised on the UI thread.
        Event<UpdateInfo> UpdateFound;

    private:
        UpdateScheduler() = default;
        void Tick();

        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_timer{ nullptr };
        bool m_checking = false;
    };

    // Seconds since the Unix epoch.
    double UnixNow() noexcept;
}
