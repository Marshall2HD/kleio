#include "pch.h"
#include "AboutPage.xaml.h"
#if __has_include("AboutPage.g.cpp")
#include "AboutPage.g.cpp"
#endif

#include "Platform/SystemIntegration.h"
#include "SettingsHelpers.h"
#include "Strings.h"

namespace winrt
{
    using namespace Microsoft::UI::Xaml;
}

namespace winrt::Kleio::implementation
{
    void AboutPage::Page_Loaded(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        try
        {
            VersionText().Text(::Kleio::Strings::Format(L"About_Version", ::Kleio::SystemIntegration::AppVersion()));
        }
        catch (...)
        {
        }
    }

    void AboutPage::CheckUpdatesButton_Click(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        ::Kleio::SettingsUi::CheckForUpdates(UpdateInfoBar(), CheckUpdatesButton());
    }
}
