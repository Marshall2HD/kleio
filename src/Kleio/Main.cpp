// Custom entry point (DISABLE_XAML_GENERATED_MAIN) so that Kleio stays single-instance: a second
// launch (Start menu, "kleio.exe --pause" alias, startup task) is redirected to the running instance,
// which handles it in App::OnActivated.
#include "pch.h"
#include "App.xaml.h"

#include <thread>

using namespace winrt;
using namespace Microsoft::Windows::AppLifecycle;

namespace
{
    constexpr wchar_t InstanceKey[] = L"Kleio.Main";

    // RedirectActivationToAsync must not be awaited on the STA thread; run it on a worker thread.
    void RedirectTo(AppInstance const& instance, AppActivationArguments const& args)
    {
        std::thread worker([&] {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            try
            {
                instance.RedirectActivationToAsync(args).get();
            }
            catch (...)
            {
            }
            winrt::uninit_apartment();
        });
        worker.join();
    }
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
    // Same as the XAML-generated main: fail fast with a clear message if the runtime is missing.
    if (HMODULE module = ::LoadLibraryW(L"Microsoft.UI.Xaml.dll"))
    {
        using CheckFn = void(WINAPI*)();
        if (auto check = reinterpret_cast<CheckFn>(::GetProcAddress(module, "XamlCheckProcessRequirements")))
        {
            check();
        }
        ::FreeLibrary(module);
    }

    winrt::init_apartment(winrt::apartment_type::single_threaded);

    auto const args = AppInstance::GetCurrent().GetActivatedEventArgs();
    auto const keyInstance = AppInstance::FindOrRegisterForKey(InstanceKey);
    if (!keyInstance.IsCurrent())
    {
        RedirectTo(keyInstance, args);
        return 0;
    }

    Microsoft::UI::Xaml::Application::Start([](auto&&) {
        winrt::make<winrt::Kleio::implementation::App>();
    });
    return 0;
}
