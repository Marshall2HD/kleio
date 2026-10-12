#pragma once

#include "App.xaml.g.h"

namespace winrt::Kleio::implementation
{
    struct App : AppT<App>
    {
        App();

        void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);

    private:
        // What HandleActivation needs, copied out of AppActivationArguments right away.
        struct Activation
        {
            Microsoft::Windows::AppLifecycle::ExtendedActivationKind kind{};
            std::wstring arguments;
            winrt::Windows::Foundation::Collections::IMap<winrt::hstring, winrt::hstring> notificationArguments{ nullptr };
        };
        static Activation Snapshot(Microsoft::Windows::AppLifecycle::AppActivationArguments const& args);

        // Handles activations redirected from a second launch (see Main.cpp). Raised on a
        // background thread; marshalled to the UI thread.
        void OnActivated(winrt::Windows::Foundation::IInspectable const&,
                         Microsoft::Windows::AppLifecycle::AppActivationArguments const& args);
        // Applies "--pause", "--resume", "--ignore-next", "--settings", "--quit", notification clicks;
        // "--background"/"--updated" and startup-task launches stay hidden; with no recognised
        // argument toggles the popup (like reopening Maccy). `initial` = first launch of the process.
        // (--get/--select/--delete/--clear never get here: see Main.cpp and Platform/ScriptingPipe.)
        void HandleActivation(Activation const& activation, bool initial);

        Microsoft::UI::Dispatching::DispatcherQueue m_dispatcher{ nullptr };
    };
}
