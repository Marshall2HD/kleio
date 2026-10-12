#include "pch.h"
#include "App.xaml.h"

#include "AppController.h"

#include <winrt/Microsoft.Windows.AppNotifications.h>

namespace winrt
{
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::Windows::AppLifecycle;
}

namespace
{
    std::wstring ArgumentsOf(winrt::AppActivationArguments const& args)
    {
        if (args.Kind() == winrt::ExtendedActivationKind::Launch)
        {
            if (auto launch = args.Data().try_as<winrt::Windows::ApplicationModel::Activation::ILaunchActivatedEventArgs>())
            {
                return std::wstring{ launch.Arguments() };
            }
        }
        return {};
    }

    bool Contains(std::wstring const& haystack, std::wstring_view needle)
    {
        return haystack.find(needle) != std::wstring::npos;
    }
}

namespace winrt::Kleio::implementation
{
    App::App()
    {
#if defined _DEBUG && !defined DISABLE_XAML_GENERATED_BREAK_ON_UNHANDLED_EXCEPTION
        UnhandledException([](IInspectable const&, UnhandledExceptionEventArgs const& e) {
            if (IsDebuggerPresent())
            {
                auto errorMessage = e.Message();
                __debugbreak();
            }
        });
#endif
        // Kleio lives in the notification area: closing the popup or Settings must not end the app.
        DispatcherShutdownMode(winrt::Microsoft::UI::Xaml::DispatcherShutdownMode::OnExplicitShutdown);
    }

    void App::OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&)
    {
        m_dispatcher = Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();

        ::Kleio::AppController::Current().Start();

        auto instance = AppInstance::GetCurrent();
        instance.Activated({ this, &App::OnActivated });
        HandleActivation(instance.GetActivatedEventArgs(), true);
    }

    void App::OnActivated(IInspectable const&, AppActivationArguments const& args)
    {
        m_dispatcher.TryEnqueue([this, args] { HandleActivation(args, false); });
    }

    void App::HandleActivation(AppActivationArguments const& args, bool initial)
    {
        auto& controller = ::Kleio::AppController::Current();
        auto& settings = ::Kleio::Settings::Instance();
        std::wstring const arguments = ArgumentsOf(args);

        // A click on one of Kleio's notifications while Kleio wasn't running (when it runs, the
        // click arrives through AppNotificationManager::NotificationInvoked instead).
        if (args.Kind() == ExtendedActivationKind::AppNotification)
        {
            if (auto notification = args.Data().try_as<winrt::Microsoft::Windows::AppNotifications::AppNotificationActivatedEventArgs>())
            {
                controller.OnNotificationInvoked(notification.Arguments());
            }
            return;
        }

        if (Contains(arguments, L"--quit"))
        {
            controller.Quit();
            return;
        }
        if (Contains(arguments, L"--ignore-next"))
        {
            controller.IgnoreNextCopy();
            return;
        }
        if (Contains(arguments, L"--pause"))
        {
            settings.IgnoreOnlyNextEvent(false);
            settings.IgnoreEvents(true);
            return;
        }
        if (Contains(arguments, L"--resume"))
        {
            settings.IgnoreOnlyNextEvent(false);
            settings.IgnoreEvents(false);
            return;
        }
        if (Contains(arguments, L"--settings"))
        {
            controller.OpenSettings();
            return;
        }

        // Started with Windows, by a scripting command (kleio --get ... starts Kleio with
        // --background) or after an update (--updated): stay in the notification area.
        if (args.Kind() == ExtendedActivationKind::StartupTask || Contains(arguments, L"--background") ||
            Contains(arguments, L"--updated"))
        {
            return;
        }
        if (initial)
        {
            controller.ShowPopup();
        }
        else
        {
            controller.TogglePopup();
        }
    }
}
