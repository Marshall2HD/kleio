#include "pch.h"
#include "App.xaml.h"

#include "AppController.h"

#include <winrt/Microsoft.Windows.AppNotifications.h>

namespace winrt
{
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::Windows::AppLifecycle;
}

namespace winrt::Kleio::implementation
{
    // Read everything out of the activation arguments immediately. For a redirected activation
    // they are a proxy into the second kleio.exe, which exits once the redirect completes; touching
    // them later on the UI thread fails with RPC_S_CALL_FAILED (and crashed Kleio).
    App::Activation App::Snapshot(AppActivationArguments const& args)
    {
        Activation activation;
        activation.kind = args.Kind();
        if (activation.kind == ExtendedActivationKind::Launch)
        {
            if (auto launch = args.Data().try_as<winrt::Windows::ApplicationModel::Activation::ILaunchActivatedEventArgs>())
            {
                activation.arguments = launch.Arguments();
            }
        }
        else if (activation.kind == ExtendedActivationKind::AppNotification)
        {
            if (auto notification = args.Data().try_as<winrt::Microsoft::Windows::AppNotifications::AppNotificationActivatedEventArgs>())
            {
                auto copy = winrt::single_threaded_map<winrt::hstring, winrt::hstring>();
                for (auto const& pair : notification.Arguments())
                {
                    copy.Insert(pair.Key(), pair.Value());
                }
                activation.notificationArguments = copy;
            }
        }
        return activation;
    }
}

namespace
{
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
        HandleActivation(Snapshot(instance.GetActivatedEventArgs()), true);
    }

    void App::OnActivated(IInspectable const&, AppActivationArguments const& args)
    {
        Activation activation;
        try
        {
            activation = Snapshot(args);
        }
        catch (...)
        {
            return; // the redirecting process is already gone; nothing to act on
        }
        m_dispatcher.TryEnqueue([this, activation = std::move(activation)] {
            try
            {
                HandleActivation(activation, false);
            }
            catch (...)
            {
                OutputDebugStringW(L"Kleio: exception while handling an activation\n");
            }
        });
    }

    void App::HandleActivation(Activation const& activation, bool initial)
    {
        auto& controller = ::Kleio::AppController::Current();
        auto& settings = ::Kleio::Settings::Instance();
        std::wstring const& arguments = activation.arguments;

        // A click on one of Kleio's notifications while Kleio wasn't running (when it runs, the
        // click arrives through AppNotificationManager::NotificationInvoked instead).
        if (activation.kind == ExtendedActivationKind::AppNotification)
        {
            controller.OnNotificationInvoked(activation.notificationArguments);
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
        if (activation.kind == ExtendedActivationKind::StartupTask || Contains(arguments, L"--background") ||
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
