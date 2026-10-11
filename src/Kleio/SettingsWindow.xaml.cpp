#include "pch.h"
#include "SettingsWindow.xaml.h"
#if __has_include("SettingsWindow.g.cpp")
#include "SettingsWindow.g.cpp"
#endif

namespace winrt
{
    using namespace Microsoft::UI::Windowing;
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::UI::Xaml::Controls;
    using namespace Microsoft::UI::Xaml::Media::Animation;
    using namespace Windows::Graphics;
}

namespace
{
    constexpr double kInitialWidth = 980;   // DIPs
    constexpr double kInitialHeight = 720;
    constexpr double kMinimumWidth = 640;
    constexpr double kMinimumHeight = 480;

    std::wstring IconPath()
    {
        wchar_t buffer[MAX_PATH]{};
        const DWORD length = GetModuleFileNameW(nullptr, buffer, ARRAYSIZE(buffer));
        std::wstring path{ buffer, length };
        auto const slash = path.find_last_of(L'\\');
        path = slash == std::wstring::npos ? std::wstring{} : path.substr(0, slash + 1);
        return path + L"Assets\\Kleio.ico";
    }
}

namespace winrt::Kleio::implementation
{
    void SettingsWindow::InitializeComponent()
    {
        SettingsWindowT<SettingsWindow>::InitializeComponent();
        SetUpWindow();

        // Release the pages when the window closes so their resources (and a shortcut recorder
        // that might still be recording) are cleaned up immediately.
        Closed([this](winrt::Windows::Foundation::IInspectable const&, winrt::WindowEventArgs const&) {
            try
            {
                ContentFrame().Content(nullptr);
            }
            catch (...)
            {
            }
        });

        // Show General unless the AppController selects another page right after construction.
        auto weak = get_weak();
        auto dispatcher = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
        if (dispatcher)
        {
            dispatcher.TryEnqueue([weak]() {
                if (auto self = weak.get())
                {
                    if (self->m_currentTag.empty())
                    {
                        self->ShowPage(L"general");
                    }
                }
            });
        }
    }

    void SettingsWindow::SetUpWindow()
    {
        try
        {
            ExtendsContentIntoTitleBar(true);
            SetTitleBar(AppTitleBar());
        }
        catch (...)
        {
            OutputDebugStringW(L"Kleio: custom title bar not available\n");
        }

        auto appWindow = AppWindow();
        if (!appWindow)
        {
            return;
        }

        try
        {
            appWindow.TitleBar().PreferredHeightOption(winrt::TitleBarHeightOption::Tall);
        }
        catch (...)
        {
        }

        try
        {
            appWindow.SetIcon(IconPath());
        }
        catch (...)
        {
            OutputDebugStringW(L"Kleio: failed to set the Settings window icon\n");
        }

        // Initial size, centred on the monitor under the mouse (where the user just clicked the
        // tray icon / popup footer).
        double scale = 1.0;
        try
        {
            POINT cursor{};
            GetCursorPos(&cursor);
            UINT dpiX = USER_DEFAULT_SCREEN_DPI;
            UINT dpiY = USER_DEFAULT_SCREEN_DPI;
            if (SUCCEEDED(GetDpiForMonitor(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), MDT_EFFECTIVE_DPI, &dpiX, &dpiY)) && dpiX > 0)
            {
                scale = static_cast<double>(dpiX) / USER_DEFAULT_SCREEN_DPI;
            }

            auto const display = winrt::DisplayArea::GetFromPoint(winrt::PointInt32{ cursor.x, cursor.y }, winrt::DisplayAreaFallback::Primary);
            auto const work = display.WorkArea();
            const int width = std::min(static_cast<int>(kInitialWidth * scale), work.Width);
            const int height = std::min(static_cast<int>(kInitialHeight * scale), work.Height);
            const int x = work.X + (work.Width - width) / 2;
            const int y = work.Y + (work.Height - height) / 2;
            appWindow.MoveAndResize(winrt::RectInt32{ x, y, width, height });
        }
        catch (...)
        {
            OutputDebugStringW(L"Kleio: failed to position the Settings window\n");
        }

        // Minimum size.
        m_minWidth = static_cast<int>(kMinimumWidth * scale);
        m_minHeight = static_cast<int>(kMinimumHeight * scale);
        try
        {
            auto presenter = appWindow.Presenter().as<winrt::OverlappedPresenter>();
            presenter.PreferredMinimumWidth(winrt::Windows::Foundation::IReference<int32_t>{ m_minWidth });
            presenter.PreferredMinimumHeight(winrt::Windows::Foundation::IReference<int32_t>{ m_minHeight });
        }
        catch (...)
        {
            m_useChangedFallback = true;
        }

        if (m_useChangedFallback)
        {
            auto weak = get_weak();
            appWindow.Changed([weak](winrt::AppWindow const&, winrt::AppWindowChangedEventArgs const& args) {
                if (!args.DidSizeChange())
                {
                    return;
                }
                if (auto self = weak.get())
                {
                    self->EnforceMinimumSize();
                }
            });
        }
    }

    void SettingsWindow::EnforceMinimumSize()
    {
        try
        {
            auto appWindow = AppWindow();
            if (auto presenter = appWindow.Presenter().try_as<winrt::OverlappedPresenter>())
            {
                if (presenter.State() != winrt::OverlappedPresenterState::Restored)
                {
                    return;
                }
            }
            auto const size = appWindow.Size();
            if (size.Width < m_minWidth || size.Height < m_minHeight)
            {
                appWindow.Resize(winrt::SizeInt32{ std::max(size.Width, m_minWidth), std::max(size.Height, m_minHeight) });
            }
        }
        catch (...)
        {
        }
    }

    void SettingsWindow::ShowPage(winrt::hstring const& tag)
    {
        try
        {
            winrt::hstring target = tag;
            auto item = FindItem(target);
            if (!item)
            {
                target = L"general";
                item = FindItem(target);
            }
            Navigate(target);
            if (item && NavView().SelectedItem() != item)
            {
                NavView().SelectedItem(item);
            }
        }
        catch (...)
        {
            OutputDebugStringW(L"Kleio: failed to show a settings page\n");
        }
    }

    void SettingsWindow::NavView_SelectionChanged(winrt::NavigationView const&, winrt::NavigationViewSelectionChangedEventArgs const& args)
    {
        try
        {
            if (auto container = args.SelectedItemContainer())
            {
                Navigate(winrt::unbox_value_or<winrt::hstring>(container.Tag(), L"general"));
            }
        }
        catch (...)
        {
        }
    }

    winrt::NavigationViewItem SettingsWindow::FindItem(winrt::hstring const& tag)
    {
        for (auto const& list : { NavView().MenuItems(), NavView().FooterMenuItems() })
        {
            for (auto const& entry : list)
            {
                if (auto item = entry.try_as<winrt::NavigationViewItem>())
                {
                    if (winrt::unbox_value_or<winrt::hstring>(item.Tag(), L"") == tag)
                    {
                        return item;
                    }
                }
            }
        }
        return nullptr;
    }

    void SettingsWindow::Navigate(winrt::hstring const& tag)
    {
        if (tag == m_currentTag)
        {
            return;
        }

        winrt::Windows::UI::Xaml::Interop::TypeName pageType{};
        if (tag == L"general") pageType = winrt::xaml_typename<winrt::Kleio::GeneralSettingsPage>();
        else if (tag == L"storage") pageType = winrt::xaml_typename<winrt::Kleio::StorageSettingsPage>();
        else if (tag == L"appearance") pageType = winrt::xaml_typename<winrt::Kleio::AppearanceSettingsPage>();
        else if (tag == L"ignore") pageType = winrt::xaml_typename<winrt::Kleio::IgnoreSettingsPage>();
        else if (tag == L"advanced") pageType = winrt::xaml_typename<winrt::Kleio::AdvancedSettingsPage>();
        else if (tag == L"about") pageType = winrt::xaml_typename<winrt::Kleio::AboutPage>();
        else return;

        m_currentTag = tag;
        ContentFrame().Navigate(pageType, nullptr, winrt::EntranceNavigationTransitionInfo{});
    }
}
