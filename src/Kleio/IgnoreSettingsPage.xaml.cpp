#include "pch.h"
#include "IgnoreSettingsPage.xaml.h"
#if __has_include("IgnoreSettingsPage.g.cpp")
#include "IgnoreSettingsPage.g.cpp"
#endif

#include <winrt/Microsoft.UI.Content.h>
#include <winrt/Microsoft.Windows.Storage.Pickers.h>
#include <winrt/Windows.Storage.Pickers.h>
#include <shobjidl_core.h>

#include "Core/Settings.h"
#include "Platform/AppInfo.h"
#include "SettingsHelpers.h"

namespace winrt
{
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::UI::Xaml::Controls;
    using namespace Microsoft::UI::Xaml::Input;
}

namespace
{
    std::wstring Trim(std::wstring_view text)
    {
        auto const first = text.find_first_not_of(L" \t\r\n");
        if (first == std::wstring_view::npos)
        {
            return {};
        }
        auto const last = text.find_last_not_of(L" \t\r\n");
        return std::wstring{ text.substr(first, last - first + 1) };
    }

    bool ContainsIgnoreCase(std::vector<std::wstring> const& list, std::wstring_view value)
    {
        return std::any_of(list.begin(), list.end(), [&](std::wstring const& entry) { return Kleio::SettingsUi::EqualsIgnoreCase(entry, value); });
    }

    // Default of Settings::IgnoredFormats (see Core/Settings.h).
    std::vector<std::wstring> DefaultIgnoredFormats()
    {
        return { L"Kleio.Transient" };
    }

    winrt::TextBlock MonospaceText(std::wstring const& text)
    {
        winrt::TextBlock block;
        block.Text(text);
        block.TextWrapping(winrt::TextWrapping::Wrap);
        block.IsTextSelectionEnabled(true);
        block.FontFamily(winrt::Microsoft::UI::Xaml::Media::FontFamily{ L"Cascadia Mono, Consolas, Courier New" });
        return block;
    }
}

namespace winrt::Kleio::implementation
{
    IgnoreSettingsPage::~IgnoreSettingsPage()
    {
        Unsubscribe();
    }

    void IgnoreSettingsPage::Page_Loaded(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        try
        {
            std::wstring always = L"Kleio always ignores copies marked with ";
            bool first = true;
            for (auto const format : ::Kleio::AlwaysIgnoredFormats)
            {
                if (!first)
                {
                    always += L" or ";
                }
                always += L"“";
                always += format;
                always += L"”";
                first = false;
            }
            always += L": the Windows conventions password managers and other apps use to keep sensitive data out of clipboard managers.";
            AlwaysIgnoredText().Text(always);

            LoadValues();
            if (!m_settingsToken)
            {
                auto weak = get_weak();
                m_settingsToken = ::Kleio::Settings::Instance().Changed.Add([weak](std::wstring_view key) {
                    if (key != ::Kleio::Settings::kIgnoreAllAppsExceptListed)
                    {
                        return;
                    }
                    if (auto self = weak.get())
                    {
                        if (!self->m_loading)
                        {
                            self->m_loading = true;
                            try
                            {
                                self->IgnoreAllExceptToggle().IsOn(::Kleio::Settings::Instance().IgnoreAllAppsExceptListed());
                            }
                            catch (...)
                            {
                            }
                            self->m_loading = false;
                        }
                    }
                });
            }
        }
        catch (...)
        {
        }
    }

    void IgnoreSettingsPage::Page_Unloaded(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        Unsubscribe();
    }

    void IgnoreSettingsPage::Unsubscribe() noexcept
    {
        if (m_settingsToken)
        {
            ::Kleio::Settings::Instance().Changed.Remove(*m_settingsToken);
            m_settingsToken.reset();
        }
    }

    void IgnoreSettingsPage::LoadValues()
    {
        m_loading = true;
        try
        {
            IgnoreAllExceptToggle().IsOn(::Kleio::Settings::Instance().IgnoreAllAppsExceptListed());
        }
        catch (...)
        {
        }
        m_loading = false;
        RebuildApps();
        RebuildFormats();
        RebuildRegexps();
    }

    void IgnoreSettingsPage::SectionBar_SelectionChanged(winrt::SelectorBar const& sender, winrt::SelectorBarSelectionChangedEventArgs const&)
    {
        try
        {
            // May be raised while the XAML is still loading (IsSelected in markup).
            if (!AppsSection() || !FormatsSection() || !RegexSection() || !AppsSectionItem() || !FormatsSectionItem() || !RegexSectionItem())
            {
                return;
            }
            auto const selected = sender.SelectedItem();
            auto visibility = [&](winrt::SelectorBarItem const& item) {
                return selected == item ? winrt::Visibility::Visible : winrt::Visibility::Collapsed;
            };
            AppsSection().Visibility(selected ? visibility(AppsSectionItem()) : winrt::Visibility::Visible);
            FormatsSection().Visibility(visibility(FormatsSectionItem()));
            RegexSection().Visibility(visibility(RegexSectionItem()));
        }
        catch (...)
        {
        }
    }

    winrt::Button IgnoreSettingsPage::MakeRemoveButton(std::wstring const& label, std::function<void()> onRemove)
    {
        winrt::Button button;
        winrt::FontIcon icon;
        icon.Glyph(L"");
        icon.FontSize(14);
        button.Content(icon);
        button.Width(40);
        button.Height(32);
        button.Padding(winrt::Thickness{ 0, 0, 0, 0 });
        winrt::Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(button, winrt::hstring{ L"Remove " + label });
        winrt::ToolTipService::SetToolTip(button, winrt::box_value(L"Remove"));
        button.Click([onRemove = std::move(onRemove)](winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&) {
            try
            {
                onRemove();
            }
            catch (...)
            {
            }
        });
        return button;
    }

    // ===== Applications =====

    void IgnoreSettingsPage::RebuildApps()
    {
        try
        {
            auto const apps = ::Kleio::Settings::Instance().IgnoredApps();
            auto children = AppsList().Children();
            children.Clear();
            double const scale = XamlRoot() ? XamlRoot().RasterizationScale() : 1.0;
            auto weak = get_weak();
            for (auto const& entry : apps)
            {
                winrt::Kleio::SettingsCard card;
                card.MinHeight(56);
                bool const isPath = entry.find_first_of(L"\\/") != std::wstring::npos;
                std::wstring name = entry;
                if (isPath)
                {
                    name = ::Kleio::AppInfo::DisplayName(entry);
                    if (name.empty())
                    {
                        name = ::Kleio::AppInfo::FileName(entry);
                    }
                    card.Description(winrt::box_value(winrt::hstring{ entry }));
                    auto png = ::Kleio::AppInfo::IconPng(entry, scale);
                    if (!png.empty())
                    {
                        winrt::ImageIcon icon;
                        icon.Source(::Kleio::SettingsUi::BitmapFromPng(std::move(png)));
                        card.HeaderIcon(icon);
                    }
                    else
                    {
                        card.Glyph(L"");
                    }
                }
                else
                {
                    card.Description(winrt::box_value(L"Executable name"));
                    card.Glyph(L"");
                }
                card.Header(winrt::box_value(winrt::hstring{ name }));
                card.Content(MakeRemoveButton(name, [weak, entry]() {
                    if (auto self = weak.get())
                    {
                        self->RemoveApp(entry);
                    }
                }));
                children.Append(card);
            }
            NoAppsText().Visibility(apps.empty() ? winrt::Visibility::Visible : winrt::Visibility::Collapsed);
        }
        catch (...)
        {
            OutputDebugStringW(L"Kleio: failed to list ignored apps\n");
        }
    }

    void IgnoreSettingsPage::AddApp(std::wstring const& path)
    {
        if (path.empty())
        {
            return;
        }
        auto& settings = ::Kleio::Settings::Instance();
        auto apps = settings.IgnoredApps();
        if (ContainsIgnoreCase(apps, path))
        {
            return;
        }
        apps.push_back(path);
        settings.IgnoredApps(apps);
        RebuildApps();
    }

    void IgnoreSettingsPage::RemoveApp(std::wstring const& entry)
    {
        auto& settings = ::Kleio::Settings::Instance();
        auto apps = settings.IgnoredApps();
        std::erase(apps, entry);
        settings.IgnoredApps(apps);
        RebuildApps();
    }

    void IgnoreSettingsPage::IgnoreAllExceptToggle_Toggled(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        if (!m_loading)
        {
            ::Kleio::Settings::Instance().IgnoreAllAppsExceptListed(IgnoreAllExceptToggle().IsOn());
        }
    }

    void IgnoreSettingsPage::RunningAppsFlyout_Opening(winrt::Windows::Foundation::IInspectable const&, winrt::Windows::Foundation::IInspectable const&)
    {
        try
        {
            auto items = RunningAppsFlyout().Items();
            items.Clear();
            auto const ignored = ::Kleio::Settings::Instance().IgnoredApps();
            double const scale = XamlRoot() ? XamlRoot().RasterizationScale() : 1.0;
            auto weak = get_weak();
            for (auto const& path : ::Kleio::SettingsUi::RunningAppPaths())
            {
                winrt::MenuFlyoutItem item;
                auto name = ::Kleio::AppInfo::DisplayName(path);
                item.Text(winrt::hstring{ name.empty() ? ::Kleio::AppInfo::FileName(path) : name });
                winrt::ToolTipService::SetToolTip(item, winrt::box_value(winrt::hstring{ path }));
                auto png = ::Kleio::AppInfo::IconPng(path, scale);
                if (!png.empty())
                {
                    winrt::ImageIcon icon;
                    icon.Source(::Kleio::SettingsUi::BitmapFromPng(std::move(png)));
                    item.Icon(icon);
                }
                item.IsEnabled(!ContainsIgnoreCase(ignored, path));
                item.Click([weak, path](winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&) {
                    if (auto self = weak.get())
                    {
                        try
                        {
                            self->AddApp(path);
                        }
                        catch (...)
                        {
                        }
                    }
                });
                items.Append(item);
            }
            if (items.Size() == 0)
            {
                winrt::MenuFlyoutItem none;
                none.Text(L"No running apps found");
                none.IsEnabled(false);
                items.Append(none);
            }
        }
        catch (...)
        {
            OutputDebugStringW(L"Kleio: failed to list running apps\n");
        }
    }

    void IgnoreSettingsPage::AddAppButton_Click(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        PickAppAsync();
    }

    winrt::fire_and_forget IgnoreSettingsPage::PickAppAsync()
    {
        auto strong = get_strong();
        auto dispatcher = this->DispatcherQueue();
        std::wstring path;
        bool pickerFailed = false;
        winrt::Microsoft::UI::WindowId windowId{};
        try
        {
            AddAppButton().IsEnabled(false);
            if (auto root = XamlRoot())
            {
                windowId = root.ContentIslandEnvironment().AppWindowId();
            }
            if (windowId.Value == 0)
            {
                windowId = winrt::Microsoft::UI::GetWindowIdFromWindow(GetActiveWindow());
            }

            winrt::Microsoft::Windows::Storage::Pickers::FileOpenPicker picker{ windowId };
            picker.FileTypeFilter().Append(L".exe");
            auto result = co_await picker.PickSingleFileAsync();
            if (result)
            {
                path = result.Path();
            }
        }
        catch (...)
        {
            pickerFailed = true;
        }

        if (pickerFailed)
        {
            // Fallback: the classic Windows.Storage picker, parented with IInitializeWithWindow.
            try
            {
                winrt::Windows::Storage::Pickers::FileOpenPicker picker;
                HWND const hwnd = winrt::Microsoft::UI::GetWindowFromWindowId(windowId);
                picker.as<::IInitializeWithWindow>()->Initialize(hwnd ? hwnd : GetActiveWindow());
                picker.FileTypeFilter().Append(L".exe");
                auto file = co_await picker.PickSingleFileAsync();
                if (file)
                {
                    path = file.Path();
                }
            }
            catch (...)
            {
                OutputDebugStringW(L"Kleio: file picker failed\n");
            }
        }

        co_await wil::resume_foreground(dispatcher);
        try
        {
            AddAppButton().IsEnabled(true);
            AddApp(path);
        }
        catch (...)
        {
        }
    }

    // ===== Clipboard formats =====

    void IgnoreSettingsPage::RebuildFormats()
    {
        try
        {
            auto formats = ::Kleio::Settings::Instance().IgnoredFormats();
            std::sort(formats.begin(), formats.end());
            auto children = FormatsList().Children();
            children.Clear();
            auto weak = get_weak();
            for (auto const& format : formats)
            {
                winrt::Kleio::SettingsCard card;
                card.MinHeight(52);
                card.Header(MonospaceText(format));
                card.Content(MakeRemoveButton(format, [weak, format]() {
                    if (auto self = weak.get())
                    {
                        self->RemoveFormat(format);
                    }
                }));
                children.Append(card);
            }
            NoFormatsText().Visibility(formats.empty() ? winrt::Visibility::Visible : winrt::Visibility::Collapsed);
        }
        catch (...)
        {
        }
    }

    void IgnoreSettingsPage::AddFormat()
    {
        auto const format = Trim(FormatTextBox().Text());
        if (format.empty())
        {
            return;
        }
        auto& settings = ::Kleio::Settings::Instance();
        auto formats = settings.IgnoredFormats();
        if (!ContainsIgnoreCase(formats, format))
        {
            formats.push_back(format);
            settings.IgnoredFormats(formats);
        }
        FormatTextBox().Text(L"");
        RebuildFormats();
    }

    void IgnoreSettingsPage::RemoveFormat(std::wstring const& format)
    {
        auto& settings = ::Kleio::Settings::Instance();
        auto formats = settings.IgnoredFormats();
        std::erase(formats, format);
        settings.IgnoredFormats(formats);
        RebuildFormats();
    }

    void IgnoreSettingsPage::FormatTextBox_KeyDown(winrt::Windows::Foundation::IInspectable const&, winrt::KeyRoutedEventArgs const& args)
    {
        if (args.Key() == winrt::Windows::System::VirtualKey::Enter)
        {
            args.Handled(true);
            try
            {
                AddFormat();
            }
            catch (...)
            {
            }
        }
    }

    void IgnoreSettingsPage::AddFormatButton_Click(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        try
        {
            AddFormat();
        }
        catch (...)
        {
        }
    }

    void IgnoreSettingsPage::ResetFormatsButton_Click(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        try
        {
            ::Kleio::Settings::Instance().IgnoredFormats(DefaultIgnoredFormats());
            RebuildFormats();
        }
        catch (...)
        {
        }
    }

    // ===== Regular expressions (Maccy's IgnoreRegexpsSettingsView) =====

    void IgnoreSettingsPage::RebuildRegexps()
    {
        try
        {
            auto const regexps = ::Kleio::Settings::Instance().IgnoreRegexps();
            auto children = RegexList().Children();
            children.Clear();
            auto weak = get_weak();
            for (auto const& regex : regexps)
            {
                winrt::Kleio::SettingsCard card;
                card.MinHeight(52);
                card.Header(MonospaceText(regex));
                card.Content(MakeRemoveButton(regex, [weak, regex]() {
                    if (auto self = weak.get())
                    {
                        self->RemoveRegex(regex);
                    }
                }));
                children.Append(card);
            }
            NoRegexText().Visibility(regexps.empty() ? winrt::Visibility::Visible : winrt::Visibility::Collapsed);
        }
        catch (...)
        {
        }
    }

    void IgnoreSettingsPage::AddRegex()
    {
        auto const regex = Trim(RegexTextBox().Text());
        if (regex.empty())
        {
            return;
        }
        try
        {
            std::wregex const compiled{ regex, std::regex_constants::ECMAScript };
            (void)compiled;
        }
        catch (std::regex_error const&)
        {
            RegexInfoBar().Message(winrt::hstring{ L"“" + regex + L"” is not a valid ECMAScript regular expression." });
            RegexInfoBar().IsOpen(true);
            return;
        }
        RegexInfoBar().IsOpen(false);

        auto& settings = ::Kleio::Settings::Instance();
        auto regexps = settings.IgnoreRegexps();
        if (std::find(regexps.begin(), regexps.end(), regex) == regexps.end())
        {
            regexps.push_back(regex);
            settings.IgnoreRegexps(regexps);
        }
        RegexTextBox().Text(L"");
        RebuildRegexps();
    }

    void IgnoreSettingsPage::RemoveRegex(std::wstring const& regex)
    {
        auto& settings = ::Kleio::Settings::Instance();
        auto regexps = settings.IgnoreRegexps();
        std::erase(regexps, regex);
        settings.IgnoreRegexps(regexps);
        RebuildRegexps();
    }

    void IgnoreSettingsPage::RegexTextBox_KeyDown(winrt::Windows::Foundation::IInspectable const&, winrt::KeyRoutedEventArgs const& args)
    {
        if (args.Key() == winrt::Windows::System::VirtualKey::Enter)
        {
            args.Handled(true);
            try
            {
                AddRegex();
            }
            catch (...)
            {
            }
        }
    }

    void IgnoreSettingsPage::AddRegexButton_Click(winrt::Windows::Foundation::IInspectable const&, winrt::RoutedEventArgs const&)
    {
        try
        {
            AddRegex();
        }
        catch (...)
        {
        }
    }
}
