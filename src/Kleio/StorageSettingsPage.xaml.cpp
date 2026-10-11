#include "pch.h"
#include "StorageSettingsPage.xaml.h"
#if __has_include("StorageSettingsPage.g.cpp")
#include "StorageSettingsPage.g.cpp"
#endif

#include "AppController.h"
#include "Core/Settings.h"
#include "SettingsHelpers.h"

#include <cmath>

namespace winrt
{
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::UI::Xaml::Controls;
}

namespace winrt::Kleio::implementation
{
    StorageSettingsPage::~StorageSettingsPage()
    {
        Unsubscribe();
    }

    void StorageSettingsPage::Page_Loaded(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        try
        {
            LoadValues();
            UpdateDatabaseSize();
            if (!m_settingsToken)
            {
                auto weak = get_weak();
                m_settingsToken = ::Kleio::Settings::Instance().Changed.Add([weak](std::wstring_view key) {
                    using S = ::Kleio::Settings;
                    if (key != S::kSaveFiles && key != S::kSaveImages && key != S::kSaveText && key != S::kHistorySize && key != S::kSortBy)
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

    void StorageSettingsPage::Page_Unloaded(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        Unsubscribe();
    }

    void StorageSettingsPage::Unsubscribe() noexcept
    {
        if (m_settingsToken)
        {
            ::Kleio::Settings::Instance().Changed.Remove(*m_settingsToken);
            m_settingsToken.reset();
        }
    }

    void StorageSettingsPage::LoadValues()
    {
        auto& settings = ::Kleio::Settings::Instance();
        m_loading = true;
        try
        {
            SaveFilesToggle().IsOn(settings.SaveFiles());
            SaveImagesToggle().IsOn(settings.SaveImages());
            SaveTextToggle().IsOn(settings.SaveText());
            HistorySizeBox().Value(settings.HistorySize());
            SortByCombo().SelectedIndex(static_cast<int32_t>(settings.SortBy()));
        }
        catch (...)
        {
        }
        m_loading = false;
    }

    void StorageSettingsPage::UpdateDatabaseSize()
    {
        try
        {
            auto const bytes = ::Kleio::AppController::Current().Storage().SizeInBytes();
            DatabaseSizeText().Text(::Kleio::SettingsUi::FormatBytes(bytes));
        }
        catch (...)
        {
            DatabaseSizeText().Text(L"");
        }
    }

    void StorageSettingsPage::SaveFilesToggle_Toggled(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        if (!m_loading)
        {
            ::Kleio::Settings::Instance().SaveFiles(SaveFilesToggle().IsOn());
        }
    }

    void StorageSettingsPage::SaveImagesToggle_Toggled(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        if (!m_loading)
        {
            ::Kleio::Settings::Instance().SaveImages(SaveImagesToggle().IsOn());
        }
    }

    void StorageSettingsPage::SaveTextToggle_Toggled(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        if (!m_loading)
        {
            ::Kleio::Settings::Instance().SaveText(SaveTextToggle().IsOn());
        }
    }

    void StorageSettingsPage::HistorySizeBox_ValueChanged(winrt::NumberBox const&, winrt::NumberBoxValueChangedEventArgs const& args)
    {
        if (m_loading)
        {
            return;
        }
        auto& settings = ::Kleio::Settings::Instance();
        double const value = args.NewValue();
        if (std::isnan(value))
        {
            // Emptied box: restore the current value.
            LoadValues();
            return;
        }
        settings.HistorySize(static_cast<std::int32_t>(std::lround(value)));
        if (HistorySizeBox().Value() != settings.HistorySize())
        {
            LoadValues(); // show the clamped value
        }
    }

    void StorageSettingsPage::SortByCombo_SelectionChanged(winrt::Windows::Foundation::IInspectable const&, winrt::SelectionChangedEventArgs const&)
    {
        if (m_loading)
        {
            return;
        }
        auto const index = SortByCombo().SelectedIndex();
        if (index >= 0)
        {
            ::Kleio::Settings::Instance().SortBy(static_cast<::Kleio::SortBy>(index));
        }
    }
}
