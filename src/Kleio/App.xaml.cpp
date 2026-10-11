#include "pch.h"
#include "App.xaml.h"
#include "PopupWindow.xaml.h"

namespace winrt
{
    using namespace Microsoft::UI::Xaml;
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
    }

    void App::OnLaunched(LaunchActivatedEventArgs const&)
    {
        m_window = make<PopupWindow>();
        m_window.Activate();
    }
}
