// Custom entry point (DISABLE_XAML_GENERATED_MAIN) so that Kleio stays single-instance: a second
// launch (Start menu, "kleio.exe --pause" alias, startup task) is redirected to the running instance,
// which handles it in App::OnActivated.
//
// Scripting verbs (--get, --select, --delete, --clear, --help) are not redirected: they need a
// reply, so the alias process talks to the running instance over a named pipe
// (Platform/ScriptingPipe), prints the result and exits with its exit code.
#include "pch.h"
#include "App.xaml.h"

#include "Core/Scripting.h"
#include "Platform/ScriptingPipe.h"

#include <thread>

using winrt::Microsoft::Windows::AppLifecycle::AppActivationArguments;
using winrt::Microsoft::Windows::AppLifecycle::AppInstance;

namespace
{
    constexpr wchar_t InstanceKey[] = L"Kleio.Main";

    // RedirectActivationToAsync must not be awaited on the STA thread; run it on a worker thread.
    // Bounded wait: if the running instance quits while handling the activation ("kleio --quit"),
    // the operation can stay pending forever and the alias process would never exit.
    void RedirectTo(AppInstance const& instance, AppActivationArguments const& args)
    {
        std::thread worker([&] {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            try
            {
                instance.RedirectActivationToAsync(args).wait_for(std::chrono::seconds{ 5 });
            }
            catch (...)
            {
            }
            winrt::uninit_apartment();
        });
        worker.join();
    }

    std::vector<std::wstring> CommandLineArguments()
    {
        std::vector<std::wstring> arguments;
        int count = 0;
        if (LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &count))
        {
            for (int i = 1; i < count; ++i)
            {
                arguments.emplace_back(argv[i]);
            }
            ::LocalFree(argv);
        }
        return arguments;
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

    auto const script = Kleio::Scripting::Parse(CommandLineArguments());
    if (!script.error.empty())
    {
        return Kleio::ScriptingPipe::PrintUsageError(script.error);
    }
    if (script.command)
    {
        return Kleio::ScriptingPipe::RunClient(*script.command);
    }

    auto const args = AppInstance::GetCurrent().GetActivatedEventArgs();
    auto const keyInstance = AppInstance::FindOrRegisterForKey(InstanceKey);
    if (!keyInstance.IsCurrent())
    {
        RedirectTo(keyInstance, args);
        return 0;
    }

    winrt::Microsoft::UI::Xaml::Application::Start([](auto&&) {
        winrt::make<winrt::Kleio::implementation::App>();
    });
    return 0;
}
