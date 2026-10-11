#include "pch.h"
#include "AdvancedSettingsPage.xaml.h"
#if __has_include("AdvancedSettingsPage.g.cpp")
#include "AdvancedSettingsPage.g.cpp"
#endif

#include "Core/Settings.h"

namespace winrt
{
    using namespace Microsoft::UI::Xaml;
}

namespace winrt::Kleio::implementation
{
    AdvancedSettingsPage::~AdvancedSettingsPage()
    {
        Unsubscribe();
    }

    void AdvancedSettingsPage::Page_Loaded(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        try
        {
            LoadValues();
            if (!m_settingsToken)
            {
                auto weak = get_weak();
                // Pause can also be toggled from the tray icon or the command line.
                m_settingsToken = ::Kleio::Settings::Instance().Changed.Add([weak](std::wstring_view key) {
                    using S = ::Kleio::Settings;
                    if (key != S::kIgnoreEvents && key != S::kClearOnQuit && key != S::kClearSystemClipboard)
                    {
                        return;
                    }
                    if (auto self = weak.get())
                    {
                        if (!self->m_loading)
                        {
                            self->LoadValues();
                        }
                    }
                });
            }
        }
        catch (...)
        {
        }
    }

    void AdvancedSettingsPage::Page_Unloaded(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        Unsubscribe();
    }

    void AdvancedSettingsPage::Unsubscribe() noexcept
    {
        if (m_settingsToken)
        {
            ::Kleio::Settings::Instance().Changed.Remove(*m_settingsToken);
            m_settingsToken.reset();
        }
    }

    void AdvancedSettingsPage::LoadValues()
    {
        auto& settings = ::Kleio::Settings::Instance();
        m_loading = true;
        try
        {
            PauseToggle().IsOn(settings.IgnoreEvents());
            ClearOnQuitToggle().IsOn(settings.ClearOnQuit());
            ClearSystemClipboardToggle().IsOn(settings.ClearSystemClipboard());
        }
        catch (...)
        {
        }
        m_loading = false;
    }

    void AdvancedSettingsPage::PauseToggle_Toggled(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        if (m_loading)
        {
            return;
        }
        auto& settings = ::Kleio::Settings::Instance();
        bool const pause = PauseToggle().IsOn();
        if (!pause)
        {
            settings.IgnoreOnlyNextEvent(false);
        }
        settings.IgnoreEvents(pause);
    }

    void AdvancedSettingsPage::ClearOnQuitToggle_Toggled(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        if (!m_loading)
        {
            ::Kleio::Settings::Instance().ClearOnQuit(ClearOnQuitToggle().IsOn());
        }
    }

    void AdvancedSettingsPage::ClearSystemClipboardToggle_Toggled(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        if (!m_loading)
        {
            ::Kleio::Settings::Instance().ClearSystemClipboard(ClearSystemClipboardToggle().IsOn());
        }
    }
}
