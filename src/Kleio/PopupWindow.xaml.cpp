#include "pch.h"
#include "PopupWindow.xaml.h"
#if __has_include("PopupWindow.g.cpp")
#include "PopupWindow.g.cpp"
#endif

#include "AppController.h"
#include "Core/ItemAction.h"
#include "HistoryItemRow.xaml.h"
#include "HistoryItemViewModel.h"
#include "PopupImages.h"
#include "PopupUiState.h"

#include <winrt/Windows.ApplicationModel.DataTransfer.h>

#include <cmath>
#include <unordered_set>

namespace xaml = winrt::Microsoft::UI::Xaml;
namespace controls = winrt::Microsoft::UI::Xaml::Controls;

namespace winrt::Kleio::implementation
{
    namespace
    {
        using ::Kleio::Modifiers;
        using winrt::Windows::Foundation::IInspectable;

        // Layout constants. They match the fixed sizes in PopupWindow.xaml / HistoryItemRow.xaml so the
        // window can be sized to its content without a layout pass (Maccy's Popup.resize).
        constexpr double kHeaderHeight = 44.0;
        constexpr double kHiddenHeaderHeight = 4.0;
        constexpr double kSeparatorHeight = 9.0;
        constexpr double kRowHeight = 32.0;
        constexpr double kFooterHeight = 9.0 + 4 * 32.0 + 4.0;
        constexpr double kEmptyStateHeight = 64.0;
        constexpr double kPasteStackChrome = 4.0 + 2.0 + 32.0 + 2.0;   // margins, border, header, padding
        constexpr std::size_t kPasteStackMaxRows = 3;
        constexpr double kSplitterWidth = 9.0;
        constexpr double kMinContentWidth = 200.0;
        constexpr double kMinPreviewWidth = 200.0;
        constexpr double kMinPreviewHeight = 320.0;
        constexpr double kMinWindowHeight = 120.0;
        constexpr std::size_t kPreviewTextLimit = 20000;
        constexpr std::size_t kEditorTextLimit = 200000;
        constexpr std::size_t kShortcutRows = 9;
        constexpr UINT_PTR kSubclassId = 0x4B4C4950;   // 'KLIP'

        constexpr Modifiers kModifierMask = Modifiers::Ctrl | Modifiers::Alt | Modifiers::Shift;

        ::Kleio::AppController& Controller()
        {
            return ::Kleio::AppController::Current();
        }

        HistoryItemViewModel* Impl(winrt::Kleio::HistoryItemViewModel const& viewModel)
        {
            return viewModel ? winrt::get_self<HistoryItemViewModel>(viewModel) : nullptr;
        }

        void Log(wchar_t const* message) noexcept
        {
            OutputDebugStringW(message);
            OutputDebugStringW(L"\n");
        }

        // Event handlers and Win32 callbacks must never let an exception escape.
        template <typename F>
        void Guard(F&& function) noexcept
        {
            try
            {
                function();
            }
            catch (winrt::hresult_error const& error)
            {
                OutputDebugStringW(L"Kleio popup: ");
                OutputDebugStringW(error.message().c_str());
                OutputDebugStringW(L"\n");
            }
            catch (...)
            {
                Log(L"Kleio popup: unexpected exception");
            }
        }

        bool IsModifierKey(uint32_t key) noexcept
        {
            switch (key)
            {
            case VK_SHIFT:
            case VK_CONTROL:
            case VK_MENU:
            case VK_LSHIFT:
            case VK_RSHIFT:
            case VK_LCONTROL:
            case VK_RCONTROL:
            case VK_LMENU:
            case VK_RMENU:
            case VK_LWIN:
            case VK_RWIN:
            case VK_CAPITAL:
                return true;
            default:
                return false;
            }
        }

        // Maccy Popup.allModifiersReleased, read from the asynchronous key state: the popup may not
        // see the key-up (e.g. it is released before the popup is focused, or Win goes to the shell).
        bool AllModifiersReleased() noexcept
        {
            for (int key : { VK_SHIFT, VK_CONTROL, VK_MENU, VK_LWIN, VK_RWIN })
            {
                if ((GetAsyncKeyState(key) & 0x8000) != 0)
                {
                    return false;
                }
            }
            return true;
        }

        // AltGr arrives as Ctrl+Alt. A Ctrl+Alt chord whose key types a character with AltGr on the
        // current layout (e.g. AltGr+N = "ń" on Polish) is text, not a Ctrl+Alt shortcut.
        bool IsAltGrCharacter(uint32_t key) noexcept
        {
            if ((GetKeyState(VK_RMENU) & 0x8000) == 0)
            {
                return false;
            }
            BYTE state[256]{};
            state[VK_CONTROL] = state[VK_LCONTROL] = 0x80;
            state[VK_MENU] = state[VK_RMENU] = 0x80;
            if ((GetKeyState(VK_SHIFT) & 0x8000) != 0)
            {
                state[VK_SHIFT] = 0x80;
            }
            HKL const layout = GetKeyboardLayout(0);
            UINT const scanCode = MapVirtualKeyExW(key, MAPVK_VK_TO_VSC, layout);
            wchar_t buffer[8]{};
            // Flag 0x4: don't change the keyboard (dead key) state.
            return ToUnicodeEx(key, scanCode, state, buffer, 8, 0x4, layout) != 0;
        }

        // Maccy KeyHandlingView.deleteLastWordFromSearch: split on spaces (empty parts dropped), drop
        // the last word, join with spaces and keep a trailing space.
        std::wstring DropLastWord(std::wstring const& query)
        {
            std::vector<std::wstring> words;
            std::size_t start = 0;
            while (start <= query.size())
            {
                std::size_t end = query.find(L' ', start);
                if (end == std::wstring::npos)
                {
                    end = query.size();
                }
                if (end > start)
                {
                    words.push_back(query.substr(start, end - start));
                }
                start = end + 1;
            }
            if (!words.empty())
            {
                words.pop_back();
            }
            std::wstring result;
            for (auto const& word : words)
            {
                result += word;
                result += L' ';
            }
            return result;
        }

        ::Kleio::KeyShortcut MakeShortcut(Modifiers modifiers, uint32_t key)
        {
            ::Kleio::KeyShortcut shortcut;
            shortcut.modifiers = modifiers;
            shortcut.key = key;
            return shortcut;
        }

        std::wstring ShortcutText(::Kleio::KeyShortcut const& shortcut)
        {
            return shortcut.IsEmpty() ? std::wstring{} : shortcut.ToString();
        }

        int ClampInt(int value, int low, int high) noexcept
        {
            if (high < low)
            {
                return low;
            }
            return std::clamp(value, low, high);
        }

        RECT WorkArea(HMONITOR monitor)
        {
            MONITORINFO info{};
            info.cbSize = sizeof(info);
            if (monitor && GetMonitorInfoW(monitor, &info))
            {
                return info.rcWork;
            }
            RECT rect{};
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &rect, 0);
            return rect;
        }

        RECT MonitorRect(HMONITOR monitor)
        {
            MONITORINFO info{};
            info.cbSize = sizeof(info);
            if (monitor && GetMonitorInfoW(monitor, &info))
            {
                return info.rcMonitor;
            }
            return WorkArea(monitor);
        }

        double ScaleFor(HMONITOR monitor)
        {
            UINT dpiX = 96;
            UINT dpiY = 96;
            if (!monitor || FAILED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)) || dpiX == 0)
            {
                dpiX = 96;
            }
            return dpiX / 96.0;
        }

        // Settings::PopupScreen: 0 = the monitor under the cursor, N = the N-th monitor.
        HMONITOR PopupScreenMonitor(POINT cursor, int screen)
        {
            if (screen > 0)
            {
                std::vector<HMONITOR> monitors;
                EnumDisplayMonitors(
                    nullptr, nullptr,
                    [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
                        reinterpret_cast<std::vector<HMONITOR>*>(data)->push_back(monitor);
                        return TRUE;
                    },
                    reinterpret_cast<LPARAM>(&monitors));
                if (static_cast<std::size_t>(screen) <= monitors.size())
                {
                    return monitors[static_cast<std::size_t>(screen) - 1];
                }
            }
            return MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
        }

        bool VisibleFrame(HWND window, RECT& rect)
        {
            if (SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect))))
            {
                return true;
            }
            return GetWindowRect(window, &rect) != FALSE;
        }

        std::wstring IconPath()
        {
            wchar_t buffer[MAX_PATH]{};
            const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
            std::wstring path(buffer, length);
            const auto slash = path.find_last_of(L"\\/");
            path.resize(slash == std::wstring::npos ? 0 : slash + 1);
            return path + L"Assets\\Kleio.ico";
        }

        // The TextBox reports line breaks as '\r'; history text usually uses "\r\n".
        std::wstring NormalizeNewlines(std::wstring_view text)
        {
            std::wstring result;
            result.reserve(text.size());
            for (std::size_t i = 0; i < text.size(); ++i)
            {
                if (text[i] == L'\r')
                {
                    result += L'\n';
                    if (i + 1 < text.size() && text[i + 1] == L'\n')
                    {
                        ++i;
                    }
                }
                else
                {
                    result += text[i];
                }
            }
            return result;
        }

        std::wstring WithNewlines(std::wstring const& normalized, bool crlf)
        {
            if (!crlf)
            {
                return normalized;
            }
            std::wstring result;
            result.reserve(normalized.size() + normalized.size() / 16);
            for (wchar_t ch : normalized)
            {
                if (ch == L'\n')
                {
                    result += L"\r\n";
                }
                else
                {
                    result += ch;
                }
            }
            return result;
        }

        bool SameObject(IInspectable const& left, IInspectable const& right)
        {
            if (!left || !right)
            {
                return false;
            }
            return left.as<winrt::Windows::Foundation::IUnknown>() == right.as<winrt::Windows::Foundation::IUnknown>();
        }

        void SetToolTip(xaml::DependencyObject const& element, std::wstring const& text)
        {
            if (text.empty())
            {
                controls::ToolTipService::SetToolTip(element, nullptr);
            }
            else
            {
                controls::ToolTipService::SetToolTip(element, winrt::box_value(winrt::hstring{ text }));
            }
        }

        xaml::Visibility VisibleIf(bool condition) noexcept
        {
            return condition ? xaml::Visibility::Visible : xaml::Visibility::Collapsed;
        }
    }

    // =====================================================================================
    // Setup
    // =====================================================================================

    PopupWindow::~PopupWindow()
    {
        try
        {
            if (m_hwnd && IsWindow(m_hwnd))
            {
                RemoveWindowSubclass(m_hwnd, &PopupWindow::SubclassProc, kSubclassId);
            }
        }
        catch (...)
        {
        }
    }

    void PopupWindow::InitializeComponent()
    {
        PopupWindowT<PopupWindow>::InitializeComponent();

        Guard([&] { ConfigureWindow(); });

        m_pinnedSource = winrt::single_threaded_observable_vector<IInspectable>();
        m_unpinnedSource = winrt::single_threaded_observable_vector<IInspectable>();
        PinnedList().ItemsSource(m_pinnedSource);
        HistoryList().ItemsSource(m_unpinnedSource);

        auto queue = this->DispatcherQueue();
        m_searchTimer = queue.CreateTimer();
        m_searchTimer.Interval(std::chrono::milliseconds(200));   // Maccy's Throttler(minimumDelay: 0.2)
        m_searchTimer.IsRepeating(false);
        m_searchTimer.Tick([this](auto&&, auto&&) { Guard([&] { ApplyQuery(); }); });

        m_previewTimer = queue.CreateTimer();
        m_previewTimer.IsRepeating(false);
        m_previewTimer.Tick([this](auto&&, auto&&) { Guard([&] { HandleAutoOpenTick(); }); });

        // Watches for the release of the popup shortcut's modifiers (Maccy Popup.handleFlagsChanged).
        m_modifierTimer = queue.CreateTimer();
        m_modifierTimer.Interval(std::chrono::milliseconds(15));
        m_modifierTimer.IsRepeating(true);
        m_modifierTimer.Tick([this](auto&&, auto&&) { Guard([&] { CheckShortcutModifiers(); }); });

        Activated({ this, &PopupWindow::HandleActivated });

        RootGrid().PreviewKeyDown({ this, &PopupWindow::HandlePreviewKeyDown });
        RootGrid().PreviewKeyUp({ this, &PopupWindow::HandlePreviewKeyUp });
        RootGrid().CharacterReceived([this](xaml::UIElement const&, xaml::Input::CharacterReceivedRoutedEventArgs const& args) {
            Guard([&] {
                // Typing while focus is elsewhere (e.g. a toolbar button) still goes to the search.
                if (args.Handled() || m_dialogOpen || !m_shown || IsSearchFocused())
                {
                    return;
                }
                const wchar_t ch = static_cast<wchar_t>(args.Character());
                if (ch < 0x20 || ch == 0x7F || ch == L' ')
                {
                    return;
                }
                std::wstring text{ SearchBox().Text() };
                text += ch;
                SearchBox().Text(text);
                FocusSearch();
                SearchBox().Select(static_cast<int32_t>(text.size()), 0);
                args.Handled(true);
            });
        });

        SearchBox().TextChanged([this](auto&&, auto&&) { Guard([&] { HandleSearchTextChanged(); }); });
        // On the very first show the tree loads after Activate(); focus the search box once it exists.
        SearchBox().Loaded([this](auto&&, auto&&) {
            Guard([&] {
                if (m_shown)
                {
                    FocusSearch();
                }
            });
        });

        HistoryList().ItemClick([this](IInspectable const&, controls::ItemClickEventArgs const& args) {
            Guard([&] { HandleItemClick(args.ClickedItem()); });
        });
        PinnedList().ItemClick([this](IInspectable const&, controls::ItemClickEventArgs const& args) {
            Guard([&] { HandleItemClick(args.ClickedItem()); });
        });
        PinnedList().DragItemsStarting([this](IInspectable const&, controls::DragItemsStartingEventArgs const&) {
            m_dragInProgress = true;
            m_previewTimer.Stop();
        });
        PinnedList().DragItemsCompleted([this](controls::ListViewBase const&, controls::DragItemsCompletedEventArgs const& args) {
            Guard([&] { HandlePinnedDragCompleted(args); });
        });

        // Maccy's panel is movable by its background: drag the header to move the popup.
        HeaderPanel().PointerPressed([this](IInspectable const&, xaml::Input::PointerRoutedEventArgs const& args) {
            Guard([&] {
                if (!m_hwnd || Controller().Settings().PopupPosition() == ::Kleio::PopupPosition::TrayIcon)
                {
                    return;
                }
                auto point = args.GetCurrentPoint(HeaderPanel());
                if (!point.Properties().IsLeftButtonPressed())
                {
                    return;
                }
                POINT cursor{};
                GetCursorPos(&cursor);
                ReleaseCapture();
                PostMessageW(m_hwnd, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(cursor.x, cursor.y));
                args.Handled(true);
            });
        });

        PreviewToggleButton().Click([this](auto&&, auto&&) {
            Guard([&] {
                TogglePreview(true);
                FocusSearch();
            });
        });

        PasteStackStopButton().Click([this](auto&&, auto&&) { Guard([&] { StopPasteStack(); }); });
        PasteStackPanel().Tapped([this](IInspectable const&, xaml::Input::TappedRoutedEventArgs const&) {
            Guard([&] {
                m_manualMulti = false;
                SelectNothing();   // nothing selected + a paste stack = the paste stack is selected
                FocusSearch();
            });
        });
        PasteStackPanel().PointerMoved([this](IInspectable const&, xaml::Input::PointerRoutedEventArgs const& args) {
            Guard([&] {
                if (!m_shown || m_dialogOpen || m_flyoutOpen || m_dragInProgress || IsMultiSelect())
                {
                    return;
                }
                if (PointerReallyMoved(args) && !PasteStackSelected())
                {
                    SelectNothing();
                }
            });
        });

        SetupFooter();
        SetupPreviewToolbar();
        SetupSplitter();

        auto weak = get_weak();
        auto& ui = ::Kleio::PopupUiState::Get();
        ui.rowPointerMoved = [weak](HistoryItemRow& row, xaml::Input::PointerRoutedEventArgs const& args) {
            if (auto self = weak.get())
            {
                Guard([&] { self->HandleRowPointerMoved(row, args); });
            }
        };
        ui.rowRightTapped = [weak](HistoryItemRow& row, xaml::Input::RightTappedRoutedEventArgs const& args) {
            if (auto self = weak.get())
            {
                Guard([&] { self->HandleRowRightTapped(row, args); });
            }
        };

        ApplyPreviewLayout();
    }

    void PopupWindow::ConfigureWindow()
    {
        if (auto native = this->try_as<::IWindowNative>())
        {
            native->get_WindowHandle(&m_hwnd);
        }

        auto appWindow = AppWindow();
        auto presenter = winrt::Microsoft::UI::Windowing::OverlappedPresenter::Create();
        presenter.IsMaximizable(false);
        presenter.IsMinimizable(false);
        presenter.IsResizable(true);
        presenter.IsAlwaysOnTop(true);
        presenter.SetBorderAndTitleBar(true, false);
        appWindow.SetPresenter(presenter);

        try
        {
            appWindow.IsShownInSwitchers(false);
        }
        catch (...)
        {
            Log(L"Kleio popup: IsShownInSwitchers failed");
        }
        try
        {
            appWindow.SetIcon(IconPath());
        }
        catch (...)
        {
            Log(L"Kleio popup: SetIcon failed");
        }

        if (m_hwnd)
        {
            DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;
            DwmSetWindowAttribute(m_hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
            SetWindowSubclass(m_hwnd, &PopupWindow::SubclassProc, kSubclassId, reinterpret_cast<DWORD_PTR>(this));
        }
    }

    void PopupWindow::EnsureInitialized()
    {
        if (m_initialized)
        {
            return;
        }
        m_initialized = true;

        auto& app = Controller();
        auto weak = get_weak();
        m_historyToken = app.History().Changed.Add([weak]() {
            if (auto self = weak.get())
            {
                Guard([&] { self->HandleHistoryChanged(); });
            }
        });
        m_settingsToken = app.Settings().Changed.Add([weak](std::wstring_view key) {
            if (auto self = weak.get())
            {
                Guard([&] { self->HandleSettingChanged(key); });
            }
        });
        m_pasteStackToken = app.PasteStackChanged.Add([weak]() {
            if (auto self = weak.get())
            {
                Guard([&] { self->HandlePasteStackChanged(); });
            }
        });
    }

    void PopupWindow::SetupFooter()
    {
        auto setup = [this](winrt::Kleio::HistoryItemRow const& row, int index, std::wstring const& title,
                            std::wstring const& shortcut, std::wstring const& tooltip) {
            winrt::get_self<HistoryItemRow>(row)->ShowFooter(winrt::hstring{ title }, winrt::hstring{ shortcut }, winrt::hstring{ tooltip });
            row.PointerMoved([this, index](IInspectable const&, xaml::Input::PointerRoutedEventArgs const& args) {
                Guard([&] { HandleFooterPointerMoved(index, args); });
            });
            row.Tapped([this, index](IInspectable const&, xaml::Input::TappedRoutedEventArgs const&) {
                Guard([&] {
                    SelectFooter(index);
                    ActivateFooter(index);
                });
            });
        };

        // Maccy Footer: clear (Opt+Cmd+Delete), clear all (Shift+Opt+Cmd+Delete), preferences (Cmd+,), about, quit (Cmd+Q).
        setup(FooterClearRow(), 0, L"Clear", ShortcutText(MakeShortcut(Modifiers::Ctrl | Modifiers::Alt, VK_BACK)),
              L"Clear unpinned items.\nHold Shift to clear all.");
        setup(FooterClearAllRow(), 1, L"Clear all", ShortcutText(MakeShortcut(kModifierMask, VK_BACK)),
              L"Clear all items, including pinned ones.");
        setup(FooterSettingsRow(), 2, L"Settings\u2026", ShortcutText(MakeShortcut(Modifiers::Ctrl, VK_OEM_COMMA)),
              L"Open Kleio settings.");
        setup(FooterAboutRow(), 3, L"About Kleio", L"", L"Read more about the application.");
        setup(FooterQuitRow(), 4, L"Quit", ShortcutText(MakeShortcut(Modifiers::Ctrl, 'Q')), L"Quit the application.");
    }

    void PopupWindow::SetupPreviewToolbar()
    {
        PreviewCopyTextButton().Click([this](auto&&, auto&&) { Guard([&] { CopyRecognizedText(); FocusSearch(); }); });
        PreviewPinButton().Click([this](auto&&, auto&&) { Guard([&] { TogglePinSelection(); FocusSearch(); }); });
        PreviewEditButton().Click([this](auto&&, auto&&) { Guard([&] { if (m_lead) OpenEditor(m_lead); }); });
        PreviewDeleteButton().Click([this](auto&&, auto&&) { Guard([&] { DeleteSelection(); FocusSearch(); }); });
        PreviewStopStackButton().Click([this](auto&&, auto&&) { Guard([&] { StopPasteStack(); FocusSearch(); }); });

        SetToolTip(PreviewCopyTextButton(), L"Copy the text recognized in the image");
        SetToolTip(PreviewEditButton(), L"Edit item");
        SetToolTip(PreviewStopStackButton(), L"Stop paste stack");
    }

    void PopupWindow::SetupSplitter()
    {
        auto splitter = PreviewSplitter();
        splitter.PointerPressed([this](IInspectable const&, xaml::Input::PointerRoutedEventArgs const& args) {
            Guard([&] {
                auto point = args.GetCurrentPoint(nullptr);
                if (!point.Properties().IsLeftButtonPressed() || !m_previewOpen)
                {
                    return;
                }
                m_splitterDragging = true;
                m_splitterStartX = point.Position().X;
                m_splitterStartWidth = PreviewPanel().ActualWidth();
                m_previewTimer.Stop();
                PreviewSplitter().CapturePointer(args.Pointer());
                args.Handled(true);
            });
        });
        splitter.PointerMoved([this](IInspectable const&, xaml::Input::PointerRoutedEventArgs const& args) {
            Guard([&] {
                if (!m_splitterDragging)
                {
                    return;
                }
                const double x = args.GetCurrentPoint(nullptr).Position().X;
                const double delta = x - m_splitterStartX;
                const double total = RootGrid().ActualWidth();
                double width = m_splitterStartWidth + (m_placementLeft ? delta : -delta);
                width = std::clamp(width, kMinPreviewWidth, std::max(kMinPreviewWidth, total - kMinContentWidth - kSplitterWidth));
                PreviewPanel().Width(width);
                args.Handled(true);
            });
        });
        auto finish = [this]() {
            if (!m_splitterDragging)
            {
                return;
            }
            m_splitterDragging = false;
            auto& settings = Controller().Settings();
            const double width = PreviewPanel().Width();
            const double total = RootGrid().ActualWidth();
            settings.PreviewWidth(width);
            if (total > 0)
            {
                settings.WindowWidth(std::max(kMinContentWidth, total - width - kSplitterWidth));
            }
            UpdateThumbnailWidth();
        };
        splitter.PointerReleased([this, finish](IInspectable const&, xaml::Input::PointerRoutedEventArgs const& args) {
            Guard([&] {
                finish();
                PreviewSplitter().ReleasePointerCapture(args.Pointer());
            });
        });
        splitter.PointerCaptureLost([finish](IInspectable const&, xaml::Input::PointerRoutedEventArgs const&) {
            Guard([&] { finish(); });
        });
    }

    LRESULT CALLBACK PopupWindow::SubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR data)
    {
        auto* self = reinterpret_cast<PopupWindow*>(data);
        switch (message)
        {
        case WM_SYSCOMMAND:
        {
            const WPARAM command = wParam & 0xFFF0;
            // A lone Alt press would put the window into menu mode and swallow the next key.
            if (command == SC_KEYMENU)
            {
                return 0;
            }
            // Alt+F4 hides instead of destroying the window.
            if (command == SC_CLOSE)
            {
                Guard([&] { self->RequestHide(); });
                return 0;
            }
            break;
        }
        case WM_ENTERSIZEMOVE:
            self->m_inSizeMove = true;
            Guard([&] {
                if (self->m_previewTimer)
                {
                    self->m_previewTimer.Stop();
                }
            });
            break;
        case WM_EXITSIZEMOVE:
            self->m_inSizeMove = false;
            Guard([&] { self->HandleSizeMoveEnded(); });
            break;
        case WM_GETMINMAXINFO:
        {
            const LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            const UINT dpi = GetDpiForWindow(hwnd);
            const double scale = dpi ? dpi / 96.0 : 1.0;
            const double minWidth = kMinContentWidth + (self->m_previewOpen ? kMinPreviewWidth + kSplitterWidth : 0.0) + 16.0;
            info->ptMinTrackSize.x = std::max<LONG>(info->ptMinTrackSize.x, static_cast<LONG>(std::lround(minWidth * scale)));
            info->ptMinTrackSize.y = std::max<LONG>(info->ptMinTrackSize.y, static_cast<LONG>(std::lround(kMinWindowHeight * scale)));
            return result;
        }
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, &PopupWindow::SubclassProc, id);
            self->m_hwnd = nullptr;
            break;
        default:
            break;
        }
        return DefSubclassProc(hwnd, message, wParam, lParam);
    }

    // =====================================================================================
    // Show / hide
    // =====================================================================================

    void PopupWindow::ShowPopup()
    {
        try
        {
            EnsureInitialized();
            if (m_settingsDirty)
            {
                ApplySettings();
            }

            ResetShortcutState();
            m_searchTimer.Stop();
            m_previewTimer.Stop();
            m_previewOpen = false;
            m_autoOpenSuppressed = false;
            m_keyboardNavigating = true;
            m_hasPointerPosition = false;
            m_manualMulti = false;
            m_dragInProgress = false;
            m_splitterDragging = false;
            m_query.clear();
            if (!SearchBox().Text().empty())
            {
                SearchBox().Text(L"");
            }
            ApplyPreviewLayout();

            const Target target = ChooseTarget();
            SetScale(target.scale);
            UpdateModifiers();

            Refresh(true);
            UpdatePasteStack();
            UpdateChrome();
            SelectInitial();
            if (!m_unpinned.empty())
            {
                HistoryList().ScrollIntoView(m_unpinned.front());
            }

            PlaceAndShow(target);
            m_shown = true;
            FocusSearch();
            Announce(m_lead);
            StartAutoOpen();
        }
        catch (winrt::hresult_error const& error)
        {
            OutputDebugStringW(L"Kleio popup: ShowPopup failed: ");
            OutputDebugStringW(error.message().c_str());
            OutputDebugStringW(L"\n");
        }
        catch (...)
        {
            Log(L"Kleio popup: ShowPopup failed");
        }
    }

    void PopupWindow::HidePopup()
    {
        if (!m_shown)
        {
            return;
        }
        Guard([&] { SaveLastPosition(); });
        m_shown = false;

        Guard([&] {
            ResetShortcutState();   // Maccy Popup.reset (panel close)
            m_searchTimer.Stop();
            m_previewTimer.Stop();
            m_previewOpen = false;
            m_splitterDragging = false;
            m_dragInProgress = false;
            m_manualMulti = false;
            AppWindow().Hide();
            ApplyPreviewLayout();

            // Maccy clears the query when the popup goes to the background.
            m_query.clear();
            SearchBox().Text(L"");
            ::Kleio::PopupUiState::Get().modifiers = Modifiers::None;
            m_previewBitmap = nullptr;
            m_previewImageId = 0;
            m_previewItemId = 0;
        });
    }

    bool PopupWindow::WasAutoHiddenWithin(uint32_t milliseconds) const noexcept
    {
        return m_autoHiddenAt != 0 && GetTickCount64() - m_autoHiddenAt <= milliseconds;
    }

    // =====================================================================================
    // Popup shortcut (Maccy Popup.handleFirstKeyDown / handleKeyDown / handleFlagsChanged)
    // =====================================================================================

    void PopupWindow::OpenedByShortcut()
    {
        if (!m_shown || !m_modifierTimer)
        {
            return;
        }
        m_shortcutState = ShortcutState::Opening;
        m_modifierTimer.Start();
        CheckShortcutModifiers();   // already released (e.g. a very short press): toggle mode
    }

    bool PopupWindow::HandleShortcutPressed(::Kleio::KeyShortcut const& shortcut)
    {
        if (!m_shown)
        {
            return false;
        }
        if (m_dialogOpen)
        {
            return true;   // Maccy: ignored while editing an item
        }

        // A pinned item whose shortcut is the popup's main key (Maccy: pressedShortcutItem first).
        if (!shortcut.IsEmpty())
        {
            if (auto viewModel = PressedShortcutItem(shortcut.key, shortcut.modifiers))
            {
                ResetShortcutState();
                SelectItem(viewModel, true);
                SelectWithModifiers(Impl(viewModel)->Item(), shortcut.modifiers);
                return true;
            }
        }

        if (m_shortcutState == ShortcutState::Opening)
        {
            m_shortcutState = ShortcutState::Cycle;
        }
        if (m_shortcutState == ShortcutState::Cycle)
        {
            KeyboardNext(true);
            return true;
        }
        return false;   // toggle: close
    }

    void PopupWindow::CheckShortcutModifiers()
    {
        if (m_shortcutState == ShortcutState::Toggle)
        {
            m_modifierTimer.Stop();
            return;
        }
        if (!m_shown)
        {
            ResetShortcutState();
            return;
        }
        if (!AllModifiersReleased())
        {
            return;
        }
        const bool cycle = m_shortcutState == ShortcutState::Cycle;
        ResetShortcutState();
        if (cycle && !m_dialogOpen)
        {
            // Releasing the modifiers in cycle mode selects (the default action, no modifiers held).
            ActivateSelection(Modifiers::None);
        }
    }

    void PopupWindow::ResetShortcutState()
    {
        m_shortcutState = ShortcutState::Toggle;
        if (m_modifierTimer)
        {
            m_modifierTimer.Stop();
        }
    }

    void PopupWindow::RequestHide()
    {
        Controller().HidePopup();
        // In case the controller didn't route back to us.
        HidePopup();
    }

    void PopupWindow::HandleActivated(IInspectable const&, xaml::WindowActivatedEventArgs const& args)
    {
        Guard([&] {
            if (args.WindowActivationState() == xaml::WindowActivationState::Deactivated)
            {
                SetModifiers(Modifiers::None);
                // Maccy's FloatingPanel.resignKey: close when focus moves elsewhere, unless a modal
                // interaction of the popup is in progress.
                if (m_shown && !m_dialogOpen && !m_flyoutOpen && !m_dragInProgress && !m_inSizeMove && !m_splitterDragging)
                {
                    m_autoHiddenAt = GetTickCount64();
                    RequestHide();
                }
            }
            else if (m_shown && !m_dialogOpen)
            {
                UpdateModifiers();
                FocusSearch();
            }
        });
    }

    // =====================================================================================
    // Geometry
    // =====================================================================================

    PopupWindow::Target PopupWindow::ChooseTarget() const
    {
        auto& app = Controller();
        auto& settings = app.Settings();
        Target target;
        target.mode = settings.PopupPosition();
        GetCursorPos(&target.cursor);

        switch (target.mode)
        {
        case ::Kleio::PopupPosition::TrayIcon:
            if (auto rect = app.TrayIcon().Rect())
            {
                target.anchor = *rect;
                target.monitor = MonitorFromRect(&*rect, MONITOR_DEFAULTTONEAREST);
            }
            else
            {
                target.mode = ::Kleio::PopupPosition::Cursor;
            }
            break;
        case ::Kleio::PopupPosition::ActiveWindow:
        {
            HWND window = app.PasteService().PreviousForegroundWindow();
            RECT frame{};
            if (window && IsWindow(window) && IsWindowVisible(window) && !IsIconic(window) && VisibleFrame(window, frame))
            {
                target.anchor = frame;
                target.monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
            }
            else
            {
                target.mode = ::Kleio::PopupPosition::ScreenCenter;
            }
            break;
        }
        default:
            break;
        }

        if (!target.monitor)
        {
            target.monitor = target.mode == ::Kleio::PopupPosition::Cursor
                                 ? MonitorFromPoint(target.cursor, MONITOR_DEFAULTTONEAREST)
                                 : PopupScreenMonitor(target.cursor, settings.PopupScreen());
        }
        target.work = WorkArea(target.monitor);
        target.scale = ScaleFor(target.monitor);
        return target;
    }

    POINT PopupWindow::ComputeOrigin(Target const& target, int width, int height)
    {
        RECT const& work = target.work;
        const int gap = static_cast<int>(std::lround(12.0 * target.scale));
        int x = target.cursor.x;
        int y = target.cursor.y;
        m_anchorBottom = false;

        switch (target.mode)
        {
        case ::Kleio::PopupPosition::TrayIcon:
            if (target.anchor)
            {
                // Like the Windows flyouts: above / below / beside the icon depending on the taskbar edge.
                const RECT icon = *target.anchor;
                const RECT monitor = MonitorRect(target.monitor);
                const int centerX = (icon.left + icon.right) / 2;
                const int centerY = (icon.top + icon.bottom) / 2;
                if (work.bottom < monitor.bottom && icon.top >= work.bottom - 1)
                {
                    x = centerX - width / 2;
                    y = work.bottom - height - gap;
                    m_anchorBottom = true;
                }
                else if (work.top > monitor.top && icon.bottom <= work.top + 1)
                {
                    x = centerX - width / 2;
                    y = work.top + gap;
                }
                else if (work.left > monitor.left && icon.right <= work.left + 1)
                {
                    x = work.left + gap;
                    y = centerY - height / 2;
                }
                else if (work.right < monitor.right && icon.left >= work.right - 1)
                {
                    x = work.right - width - gap;
                    y = centerY - height / 2;
                }
                else if (centerY > (monitor.top + monitor.bottom) / 2)
                {
                    // Auto-hidden taskbar / overflow area: open above the icon.
                    x = centerX - width / 2;
                    y = icon.top - height - gap;
                    m_anchorBottom = true;
                }
                else
                {
                    x = centerX - width / 2;
                    y = icon.bottom + gap;
                }
            }
            break;
        case ::Kleio::PopupPosition::ActiveWindow:
            if (target.anchor)
            {
                const RECT frame = *target.anchor;
                x = (frame.left + frame.right) / 2 - width / 2;
                y = (frame.top + frame.bottom) / 2 - height / 2;
            }
            break;
        case ::Kleio::PopupPosition::ScreenCenter:
            x = (work.left + work.right) / 2 - width / 2;
            y = (work.top + work.bottom) / 2 - height / 2;
            break;
        case ::Kleio::PopupPosition::LastPosition:
        {
            // Relative position of the top-middle anchor within the work area.
            auto& settings = Controller().Settings();
            const double relativeX = std::clamp(settings.WindowPositionX(), 0.0, 1.0);
            const double relativeY = std::clamp(settings.WindowPositionY(), 0.0, 1.0);
            x = work.left + static_cast<int>(std::lround((work.right - work.left) * relativeX)) - width / 2;
            y = work.top + static_cast<int>(std::lround((work.bottom - work.top) * relativeY));
            break;
        }
        case ::Kleio::PopupPosition::Cursor:
        default:
            // Top-left corner at the mouse cursor.
            x = target.cursor.x;
            y = target.cursor.y;
            break;
        }

        // Never spill over to an adjacent screen.
        x = ClampInt(x, work.left, work.right - width);
        y = ClampInt(y, work.top, work.bottom - height);
        return POINT{ x, y };
    }

    void PopupWindow::PlaceAndShow(Target const& target)
    {
        auto& settings = Controller().Settings();
        auto place = [&](double scale) {
            const double workWidth = (target.work.right - target.work.left) / scale;
            const double workHeight = (target.work.bottom - target.work.top) / scale;
            const double width = std::clamp(settings.WindowWidth(), kMinContentWidth, std::max(kMinContentWidth, workWidth));
            const double height = DesiredHeight(workHeight);
            const int widthPx = static_cast<int>(std::lround(width * scale));
            const int heightPx = static_cast<int>(std::lround(height * scale));
            const POINT origin = ComputeOrigin(target, widthPx, heightPx);
            SetClientRect(origin.x, origin.y, widthPx, heightPx);
        };

        place(m_scale);
        this->Activate();

        // Moving to a monitor with another DPI rescales the window: place it again with the real DPI.
        if (m_hwnd)
        {
            const UINT dpi = GetDpiForWindow(m_hwnd);
            if (dpi != 0 && std::abs(dpi / 96.0 - m_scale) > 0.01)
            {
                SetScale(dpi / 96.0);
                place(m_scale);
            }
        }
        BringToFront();
    }

    void PopupWindow::BringToFront()
    {
        if (!m_hwnd || GetForegroundWindow() == m_hwnd)
        {
            return;
        }
        // The app was activated by a hot key / tray click, so it may take the foreground.
        SetForegroundWindow(m_hwnd);
        if (GetForegroundWindow() == m_hwnd)
        {
            return;
        }
        HWND foreground = GetForegroundWindow();
        const DWORD foregroundThread = foreground ? GetWindowThreadProcessId(foreground, nullptr) : 0;
        const DWORD currentThread = GetCurrentThreadId();
        if (foregroundThread != 0 && foregroundThread != currentThread && AttachThreadInput(currentThread, foregroundThread, TRUE))
        {
            BringWindowToTop(m_hwnd);
            SetForegroundWindow(m_hwnd);
            AttachThreadInput(currentThread, foregroundThread, FALSE);
        }
    }

    double PopupWindow::HeaderHeight() const noexcept
    {
        return m_headerVisible ? kHeaderHeight : kHiddenHeaderHeight;
    }

    double PopupWindow::ContentHeight()
    {
        auto& ui = ::Kleio::PopupUiState::Get();
        double height = HeaderHeight();

        const bool stackVisible = PasteStackVisible();
        if (stackVisible)
        {
            const std::size_t count = Controller().CurrentPasteStack()->items.size();
            height += kPasteStackChrome + kRowHeight * static_cast<double>(std::min(count, kPasteStackMaxRows));
        }

        double pins = 0;
        for (auto const& viewModel : m_pinned)
        {
            pins += Impl(viewModel)->RowHeight(ui.imageMaxHeight, ui.thumbnailMaxWidth, ui.scale);
        }
        double list = 0;
        for (auto const& viewModel : m_unpinned)
        {
            list += Impl(viewModel)->RowHeight(ui.imageMaxHeight, ui.thumbnailMaxWidth, ui.scale);
            if (list > 10000)
            {
                break;   // far beyond any window height
            }
        }

        const bool pinsVisible = !m_pinned.empty();
        const bool historyEmpty = m_unpinned.empty();
        height += pins + list;
        if (!historyEmpty && ((m_pinTop && pinsVisible) || stackVisible))
        {
            height += kSeparatorHeight;
        }
        if (!historyEmpty && !m_pinTop && pinsVisible)
        {
            height += kSeparatorHeight;
        }
        if (historyEmpty && !pinsVisible)
        {
            height += kEmptyStateHeight;
        }
        if (m_footerVisible)
        {
            height += kFooterHeight;
        }
        return height + 2.0;
    }

    double PopupWindow::DesiredHeight(double workHeight)
    {
        // Maccy Popup.preferredHeight: room for at least 3 items, at most the configured height.
        double minimum = HeaderHeight() + 3 * kRowHeight + (m_footerVisible ? kFooterHeight : 0.0);
        if (m_previewOpen)
        {
            minimum = std::max(minimum, kMinPreviewHeight);
        }
        double height = std::max(ContentHeight(), minimum);
        height = std::min(height, std::max(kMinWindowHeight, Controller().Settings().WindowHeight()));
        height = std::min(height, workHeight);
        return std::max(height, kMinWindowHeight);
    }

    void PopupWindow::FitHeight()
    {
        if (!m_shown || m_inSizeMove || m_dialogOpen || !m_hwnd)
        {
            return;
        }
        RECT client{};
        if (!GetClientScreenRect(client))
        {
            return;
        }
        const RECT work = WorkArea(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST));
        const int height = static_cast<int>(std::lround(DesiredHeight((work.bottom - work.top) / m_scale) * m_scale));
        const int currentHeight = client.bottom - client.top;
        if (std::abs(height - currentHeight) <= 1)
        {
            return;
        }
        // Keep the top edge (Maccy verticallyResize); popups opened above the tray icon keep their bottom.
        int y = m_anchorBottom ? client.bottom - height : client.top;
        y = ClampInt(y, work.top, work.bottom - height);
        SetClientRect(client.left, y, client.right - client.left, height);
    }

    bool PopupWindow::GetClientScreenRect(RECT& rect) const
    {
        if (!m_hwnd)
        {
            return false;
        }
        RECT client{};
        POINT origin{ 0, 0 };
        if (!::GetClientRect(m_hwnd, &client) || !ClientToScreen(m_hwnd, &origin))
        {
            return false;
        }
        rect = RECT{ origin.x, origin.y, origin.x + client.right, origin.y + client.bottom };
        return true;
    }

    void PopupWindow::SetClientRect(int x, int y, int width, int height)
    {
        if (!m_hwnd)
        {
            return;
        }
        RECT window{};
        RECT client{};
        POINT origin{ 0, 0 };
        int offsetX = 0;
        int offsetY = 0;
        int extraWidth = 0;
        int extraHeight = 0;
        if (::GetWindowRect(m_hwnd, &window) && ::GetClientRect(m_hwnd, &client) && ClientToScreen(m_hwnd, &origin))
        {
            offsetX = origin.x - window.left;
            offsetY = origin.y - window.top;
            extraWidth = std::max(0, static_cast<int>(window.right - window.left) - static_cast<int>(client.right));
            extraHeight = std::max(0, static_cast<int>(window.bottom - window.top) - static_cast<int>(client.bottom));
        }
        winrt::Windows::Graphics::RectInt32 rect{ x - offsetX, y - offsetY, width + extraWidth, height + extraHeight };
        AppWindow().MoveAndResize(rect);
        m_lastClientWidth = width;
        m_lastClientHeight = height;
    }

    void PopupWindow::SetScale(double scale)
    {
        m_scale = scale > 0 ? scale : 1.0;
        ::Kleio::PopupUiState::Get().scale = m_scale;
    }

    void PopupWindow::SaveLastPosition()
    {
        auto& settings = Controller().Settings();
        if (settings.PopupPosition() != ::Kleio::PopupPosition::LastPosition)
        {
            return;
        }
        RECT client{};
        if (!GetClientScreenRect(client))
        {
            return;
        }
        const RECT work = WorkArea(MonitorFromRect(&client, MONITOR_DEFAULTTONEAREST));
        const double workWidth = static_cast<double>(work.right - work.left);
        const double workHeight = static_cast<double>(work.bottom - work.top);
        if (workWidth <= 0 || workHeight <= 0)
        {
            return;
        }
        // Only the list part counts (Maccy stores the position without the preview).
        int contentLeft = client.left;
        int contentWidth = client.right - client.left;
        if (m_previewOpen)
        {
            const int previewPx = static_cast<int>(std::lround((PreviewPanel().Width() + kSplitterWidth) * m_scale));
            contentWidth = std::max(1, contentWidth - previewPx);
            if (m_placementLeft)
            {
                contentLeft += previewPx;
            }
        }
        settings.WindowPositionX(std::clamp((contentLeft + contentWidth / 2.0 - work.left) / workWidth, 0.0, 1.0));
        settings.WindowPositionY(std::clamp((client.top - work.top) / workHeight, 0.0, 1.0));
    }

    void PopupWindow::HandleSizeMoveEnded()
    {
        if (!m_shown)
        {
            return;
        }
        RECT client{};
        if (!GetClientScreenRect(client))
        {
            return;
        }
        const int width = client.right - client.left;
        const int height = client.bottom - client.top;
        auto& settings = Controller().Settings();
        if (width != m_lastClientWidth)
        {
            const double preview = m_previewOpen ? PreviewPanel().Width() + kSplitterWidth : 0.0;
            settings.WindowWidth(std::max(kMinContentWidth, width / m_scale - preview));
            UpdateThumbnailWidth();
        }
        if (height != m_lastClientHeight)
        {
            settings.WindowHeight(std::max(kMinWindowHeight, height / m_scale));
        }
        m_lastClientWidth = width;
        m_lastClientHeight = height;
        SaveLastPosition();
        StartAutoOpen();
    }

    // =====================================================================================
    // Settings and chrome
    // =====================================================================================

    void PopupWindow::ApplySettings()
    {
        m_settingsDirty = false;
        auto& settings = Controller().Settings();
        auto& ui = ::Kleio::PopupUiState::Get();
        ui.showApplicationIcons = settings.ShowApplicationIcons();
        ui.showHexColorSwatch = settings.ShowHexColorSwatch();
        ui.imageMaxHeight = static_cast<double>(settings.ImageMaxHeight());
        ui.highlightMatch = settings.HighlightMatch();
        ui.pasteByDefault = settings.PasteByDefault();
        ui.removeFormattingByDefault = settings.RemoveFormattingByDefault();
        UpdateThumbnailWidth();

        m_pinTop = settings.PinTo() == ::Kleio::PinsPosition::Top;
        controls::Grid::SetRow(PinnedList(), m_pinTop ? 2 : 6);

        m_footerVisible = settings.ShowFooter();
        FooterPanel().Visibility(VisibleIf(m_footerVisible));
        if (!m_footerVisible && m_footerSelection >= 0)
        {
            m_footerSelection = -1;
            UpdateFooterSelection();
        }

        UpdateHeaderVisibility();
        UpdatePreviewToggleTooltip();

        for (auto const& entry : m_cache)
        {
            Impl(entry.second)->NotifyChanged();
        }
    }

    void PopupWindow::HandleSettingChanged(std::wstring_view key)
    {
        using S = ::Kleio::Settings;
        static constexpr std::wstring_view relevant[] = {
            S::kShowTitle, S::kShowSearch, S::kSearchVisibility, S::kShowFooter, S::kPinTo,
            S::kShowApplicationIcons, S::kShowHexColorSwatch, S::kImageMaxHeight, S::kHighlightMatch,
            S::kPasteByDefault, S::kRemoveFormattingByDefault, S::kPinShortcut, S::kDeleteShortcut,
            S::kTogglePreviewShortcut, S::kSearchMode, S::kSortBy, S::kOpenPreviewAutomatically,
            S::kShowSpecialSymbols,
        };
        if (std::find(std::begin(relevant), std::end(relevant), key) == std::end(relevant))
        {
            return;
        }
        m_settingsDirty = true;
        if (!m_shown)
        {
            return;
        }
        ApplySettings();
        RefreshPreservingSelection();
    }

    bool PopupWindow::UpdateHeaderVisibility()
    {
        auto& settings = Controller().Settings();
        const bool typing = !SearchBox().Text().empty();
        // Maccy AppState.searchVisible. When hidden the search box keeps the focus (clipped), so typing
        // still filters.
        const bool visible = settings.ShowSearch() &&
                             (settings.SearchVisibility() == ::Kleio::SearchVisibility::Always || typing);
        HeaderTitle().Visibility(VisibleIf(settings.ShowTitle()));
        if (visible == m_headerVisible && HeaderPanel().Height() == HeaderHeight())
        {
            return false;
        }
        m_headerVisible = visible;
        HeaderPanel().Height(HeaderHeight());
        HeaderPanel().Opacity(visible ? 1.0 : 0.0);
        HeaderPanel().IsHitTestVisible(visible);
        return true;
    }

    void PopupWindow::UpdateChrome()
    {
        const bool pinsVisible = !m_pinned.empty();
        const bool historyEmpty = m_unpinned.empty();
        const bool stackVisible = PasteStackVisible();

        PinnedList().Visibility(VisibleIf(pinsVisible));
        TopSeparator().Visibility(VisibleIf(!historyEmpty && ((m_pinTop && pinsVisible) || stackVisible)));
        BottomSeparator().Visibility(VisibleIf(!historyEmpty && !m_pinTop && pinsVisible));

        if (historyEmpty && !pinsVisible)
        {
            EmptyText().Text(m_query.empty() ? L"Copy something to see it here." : L"No results");
            EmptyText().Visibility(xaml::Visibility::Visible);
        }
        else
        {
            EmptyText().Visibility(xaml::Visibility::Collapsed);
        }
        UpdateHeaderVisibility();
    }

    void PopupWindow::UpdateThumbnailWidth()
    {
        auto& ui = ::Kleio::PopupUiState::Get();
        ui.thumbnailMaxWidth = std::max(60.0, Controller().Settings().WindowWidth() - 110.0);
    }

    void PopupWindow::UpdatePreviewToggleTooltip()
    {
        const auto shortcut = ShortcutText(Controller().Settings().TogglePreviewShortcut());
        std::wstring text;
        if (shortcut.empty())
        {
            text = m_previewOpen ? L"Hide preview" : L"Show preview";
        }
        else
        {
            text = L"Press " + shortcut + (m_previewOpen ? L" to hide preview." : L" to show preview.");
        }
        SetToolTip(PreviewToggleButton(), text);
    }

    // =====================================================================================
    // Data
    // =====================================================================================

    winrt::Kleio::HistoryItemViewModel PopupWindow::Acquire(::Kleio::HistoryItemPtr const& item)
    {
        if (auto it = m_cache.find(item->id); it != m_cache.end())
        {
            return it->second;
        }
        auto viewModel = winrt::make<HistoryItemViewModel>(item);
        m_cache.emplace(item->id, viewModel);
        return viewModel;
    }

    void PopupWindow::Refresh(bool historyChanged)
    {
        auto& history = Controller().History();

        std::vector<::Kleio::SearchResult> pinnedResults;
        std::vector<::Kleio::SearchResult> unpinnedResults;
        if (m_query.empty())
        {
            for (auto const& item : history.PinnedItems())
            {
                pinnedResults.push_back(::Kleio::SearchResult{ item, std::nullopt, {} });
            }
            for (auto const& item : history.UnpinnedItems())
            {
                unpinnedResults.push_back(::Kleio::SearchResult{ item, std::nullopt, {} });
            }
        }
        else
        {
            auto filtered = history.Filter(m_query);
            pinnedResults = std::move(filtered.pinned);
            unpinnedResults = std::move(filtered.unpinned);
        }

        std::vector<ViewModel> pinned;
        pinned.reserve(pinnedResults.size());
        for (auto const& result : pinnedResults)
        {
            if (!result.item)
            {
                continue;
            }
            auto viewModel = Acquire(result.item);
            if (Impl(viewModel)->Update(result.item, result.ranges, result.item->pin))
            {
                Impl(viewModel)->NotifyChanged();
            }
            pinned.push_back(viewModel);
        }

        // Maccy updateUnpinnedShortcuts: the first 9 visible unpinned items get 1..9.
        std::vector<ViewModel> unpinned;
        unpinned.reserve(unpinnedResults.size());
        std::size_t index = 0;
        for (auto const& result : unpinnedResults)
        {
            if (!result.item)
            {
                continue;
            }
            auto viewModel = Acquire(result.item);
            const std::wstring key = index < kShortcutRows ? std::to_wstring(index + 1) : std::wstring{};
            ++index;
            if (Impl(viewModel)->Update(result.item, result.ranges, key))
            {
                Impl(viewModel)->NotifyChanged();
            }
            unpinned.push_back(viewModel);
        }

        SyncSource(m_pinnedSource, m_pinnedMirror, pinned);
        SyncSource(m_unpinnedSource, m_unpinnedMirror, unpinned);
        m_pinned = std::move(pinned);
        m_unpinned = std::move(unpinned);

        m_visible.clear();
        m_visible.reserve(m_pinned.size() + m_unpinned.size());
        auto& first = m_pinTop ? m_pinned : m_unpinned;
        auto& second = m_pinTop ? m_unpinned : m_pinned;
        m_visible.insert(m_visible.end(), first.begin(), first.end());
        m_visible.insert(m_visible.end(), second.begin(), second.end());

        if (historyChanged)
        {
            PruneCache();
        }
        UpdateChrome();
    }

    void PopupWindow::PruneCache()
    {
        auto& history = Controller().History();
        std::unordered_set<std::int64_t> alive;
        for (auto const& item : history.PinnedItems())
        {
            alive.insert(item->id);
        }
        for (auto const& item : history.UnpinnedItems())
        {
            alive.insert(item->id);
        }
        for (auto it = m_cache.begin(); it != m_cache.end();)
        {
            if (!alive.contains(it->first))
            {
                it = m_cache.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void PopupWindow::SyncSource(SourceVector const& source, std::vector<HistoryItemViewModel*>& mirror, std::vector<ViewModel> const& desired)
    {
        std::vector<HistoryItemViewModel*> wanted;
        wanted.reserve(desired.size());
        for (auto const& viewModel : desired)
        {
            wanted.push_back(Impl(viewModel));
        }
        if (wanted == mirror)
        {
            return;
        }

        const std::unordered_set<HistoryItemViewModel*> wantedSet(wanted.begin(), wanted.end());
        std::size_t common = 0;
        for (auto* viewModel : mirror)
        {
            if (wantedSet.contains(viewModel))
            {
                ++common;
            }
        }

        // Large changes (search, clear): one reset instead of many notifications.
        const std::size_t changes = (mirror.size() - common) + (wanted.size() - common);
        if (common == 0 || changes > 40)
        {
            std::vector<IInspectable> items;
            items.reserve(desired.size());
            for (auto const& viewModel : desired)
            {
                items.push_back(viewModel);
            }
            source.ReplaceAll(items);
            mirror = std::move(wanted);
            return;
        }

        // Small changes (new copy, delete, pin): keep the realized rows.
        for (std::size_t i = mirror.size(); i-- > 0;)
        {
            if (!wantedSet.contains(mirror[i]))
            {
                source.RemoveAt(static_cast<uint32_t>(i));
                mirror.erase(mirror.begin() + static_cast<std::ptrdiff_t>(i));
            }
        }
        for (std::size_t i = 0; i < wanted.size(); ++i)
        {
            if (i < mirror.size() && mirror[i] == wanted[i])
            {
                continue;
            }
            auto it = std::find(mirror.begin() + static_cast<std::ptrdiff_t>(std::min(i, mirror.size())), mirror.end(), wanted[i]);
            if (it != mirror.end())
            {
                source.RemoveAt(static_cast<uint32_t>(it - mirror.begin()));
                mirror.erase(it);
            }
            const std::size_t position = std::min(i, mirror.size());
            source.InsertAt(static_cast<uint32_t>(position), desired[i]);
            mirror.insert(mirror.begin() + static_cast<std::ptrdiff_t>(position), wanted[i]);
        }
        while (mirror.size() > wanted.size())
        {
            source.RemoveAt(static_cast<uint32_t>(mirror.size() - 1));
            mirror.pop_back();
        }
    }

    void PopupWindow::RebuildPinnedMirror()
    {
        m_pinnedMirror.clear();
        const uint32_t size = m_pinnedSource.Size();
        for (uint32_t i = 0; i < size; ++i)
        {
            auto viewModel = m_pinnedSource.GetAt(i).try_as<ViewModel>();
            m_pinnedMirror.push_back(Impl(viewModel));
        }
    }

    void PopupWindow::RefreshPreservingSelection()
    {
        std::vector<std::int64_t> selectedIds;
        for (auto const& viewModel : m_selection)
        {
            selectedIds.push_back(Impl(viewModel)->Id());
        }
        const std::int64_t leadId = m_lead ? Impl(m_lead)->Id() : 0;
        const int leadIndex = m_lead ? VisibleIndex(m_lead) : -1;
        const int footer = m_footerSelection;
        const bool stackSelected = PasteStackSelected();

        Refresh(true);
        UpdatePasteStack();
        UpdateChrome();

        ViewModel pending{ nullptr };
        if (m_pendingSelectId)
        {
            pending = FindVisible(*m_pendingSelectId);
            m_pendingSelectId.reset();
        }

        if (pending)
        {
            SelectItem(pending, true);
        }
        else if (leadId != 0)
        {
            if (auto lead = FindVisible(leadId))
            {
                std::vector<ViewModel> selection;
                for (auto id : selectedIds)
                {
                    if (auto viewModel = FindVisible(id))
                    {
                        selection.push_back(viewModel);
                    }
                }
                if (selection.empty())
                {
                    selection.push_back(lead);
                }
                SetSelection(std::move(selection), lead, -1, false);
            }
            else if (!m_visible.empty())
            {
                // Maccy selects the nearest remaining item.
                const int index = ClampInt(leadIndex, 0, static_cast<int>(m_visible.size()) - 1);
                SelectItem(m_visible[static_cast<std::size_t>(index)], true);
            }
            else
            {
                SelectNothing();
            }
        }
        else if (footer >= 0 && m_footerVisible)
        {
            SetSelection({}, nullptr, footer, false);
        }
        else if (stackSelected && PasteStackVisible())
        {
            SelectNothing();
        }
        else
        {
            SelectInitial();
        }

        UpdatePreview();
        FitHeight();
    }

    void PopupWindow::HandleHistoryChanged()
    {
        if (!m_shown)
        {
            return;   // rebuilt on the next ShowPopup
        }
        if (m_batchDepth > 0)
        {
            m_refreshPending = true;
            return;
        }
        RefreshPreservingSelection();
    }

    void PopupWindow::HandlePasteStackChanged()
    {
        if (!m_shown)
        {
            return;
        }
        UpdatePasteStack();
        UpdateChrome();
        if (!m_lead && m_footerSelection < 0 && !PasteStackVisible())
        {
            SelectInitial();
        }
        UpdatePreview();
        FitHeight();
    }

    bool PopupWindow::PasteStackVisible() const
    {
        auto const& stack = Controller().CurrentPasteStack();
        return stack.has_value() && !stack->items.empty();
    }

    void PopupWindow::UpdatePasteStack()
    {
        auto const& stack = Controller().CurrentPasteStack();
        const bool visible = stack.has_value() && !stack->items.empty();
        PasteStackPanel().Visibility(VisibleIf(visible));
        auto children = PasteStackItems().Children();
        children.Clear();
        if (visible)
        {
            const std::size_t count = stack->items.size();
            PasteStackTitle().Text(L"Paste stack \u00B7 " + std::to_wstring(count) + (count == 1 ? L" item" : L" items"));
            for (std::size_t i = 0; i < count && i < kPasteStackMaxRows; ++i)
            {
                if (!stack->items[i])
                {
                    continue;
                }
                auto row = winrt::make<HistoryItemRow>();
                winrt::get_self<HistoryItemRow>(row)->ShowStatic(winrt::make<HistoryItemViewModel>(stack->items[i]), static_cast<int>(i + 1));
                children.Append(row);
            }
        }
        UpdatePasteStackSelection();
    }

    void PopupWindow::UpdatePasteStackSelection()
    {
        const bool selected = PasteStackSelected();
        PasteStackSelectionFill().Opacity(selected ? 1.0 : 0.0);
        PasteStackSelectionPill().Visibility(VisibleIf(selected));
    }

    // =====================================================================================
    // Search
    // =====================================================================================

    void PopupWindow::HandleSearchTextChanged()
    {
        if (!m_shown)
        {
            return;
        }
        if (UpdateHeaderVisibility())
        {
            FitHeight();
        }
        if (std::wstring{ SearchBox().Text() } == m_query)
        {
            return;
        }
        if (!m_searchTimer.IsRunning())
        {
            m_searchTimer.Start();
        }
    }

    void PopupWindow::ApplyQuery()
    {
        if (!m_shown)
        {
            return;
        }
        std::wstring text{ SearchBox().Text() };
        if (text == m_query)
        {
            return;
        }
        m_query = std::move(text);
        Refresh(false);

        // Maccy History.searchQuery didSet.
        m_keyboardNavigating = true;
        m_manualMulti = false;
        if (m_query.empty())
        {
            SelectFirstUnpinned();
        }
        else
        {
            KeyboardFirst();
        }

        // Reordering pins only makes sense on the full list.
        const bool reorderable = m_query.empty();
        PinnedList().CanReorderItems(reorderable);
        PinnedList().CanDragItems(reorderable);

        UpdatePreview();
        FitHeight();
    }

    void PopupWindow::SetQueryNow(std::wstring const& text)
    {
        SearchBox().Text(text);
        m_searchTimer.Stop();
        ApplyQuery();
        UpdateHeaderVisibility();
        FitHeight();
    }

    void PopupWindow::FocusSearch()
    {
        if (!m_dialogOpen)
        {
            SearchBox().Focus(xaml::FocusState::Programmatic);
        }
    }

    bool PopupWindow::IsSearchFocused()
    {
        auto root = Content() ? Content().XamlRoot() : nullptr;
        if (!root)
        {
            return false;
        }
        auto focused = xaml::Input::FocusManager::GetFocusedElement(root);
        return SameObject(focused, SearchBox());
    }

    // =====================================================================================
    // Selection
    // =====================================================================================

    bool PopupWindow::IsMultiSelect() const noexcept
    {
        return m_manualMulti || m_selection.size() > 1;
    }

    bool PopupWindow::PasteStackSelected() const
    {
        return !m_lead && m_footerSelection < 0 && PasteStackVisible();
    }

    int PopupWindow::VisibleIndex(ViewModel const& viewModel) const
    {
        if (!viewModel)
        {
            return -1;
        }
        auto it = std::find(m_visible.begin(), m_visible.end(), viewModel);
        return it == m_visible.end() ? -1 : static_cast<int>(it - m_visible.begin());
    }

    winrt::Kleio::HistoryItemViewModel PopupWindow::FindVisible(std::int64_t id) const
    {
        for (auto const& viewModel : m_visible)
        {
            if (Impl(viewModel)->Id() == id)
            {
                return viewModel;
            }
        }
        return nullptr;
    }

    std::vector<int> PopupWindow::FooterItems() const
    {
        if (!m_footerVisible)
        {
            return {};
        }
        return { m_showClearAll ? 1 : 0, 2, 3, 4 };
    }

    HistoryItemRow* PopupWindow::FooterRow(int index)
    {
        switch (index)
        {
        case 0: return winrt::get_self<HistoryItemRow>(FooterClearRow());
        case 1: return winrt::get_self<HistoryItemRow>(FooterClearAllRow());
        case 2: return winrt::get_self<HistoryItemRow>(FooterSettingsRow());
        case 3: return winrt::get_self<HistoryItemRow>(FooterAboutRow());
        case 4: return winrt::get_self<HistoryItemRow>(FooterQuitRow());
        default: return nullptr;
        }
    }

    void PopupWindow::UpdateFooterSelection()
    {
        for (int i = 0; i < 5; ++i)
        {
            if (auto* row = FooterRow(i))
            {
                row->SetFooterSelected(i == m_footerSelection);
            }
        }
    }

    void PopupWindow::SetSelection(std::vector<ViewModel> selection, ViewModel const& lead, int footer, bool scroll)
    {
        const bool wasMulti = IsMultiSelect();
        const std::size_t previousCount = m_selection.size();

        for (auto const& viewModel : m_selection)
        {
            if (std::find(selection.begin(), selection.end(), viewModel) == selection.end())
            {
                Impl(viewModel)->SetSelectionIndex(-1);
            }
        }
        m_selection = std::move(selection);
        if (m_selection.empty())
        {
            m_manualMulti = false;
        }
        const bool isMulti = IsMultiSelect();
        auto& ui = ::Kleio::PopupUiState::Get();
        ui.multiSelect = isMulti;
        ui.selectionCount = m_selection.size();
        // Re-render every selected row when the badges or "Selected, i of n" labels change.
        const bool forceRender = wasMulti != isMulti || (isMulti && previousCount != m_selection.size());
        for (std::size_t i = 0; i < m_selection.size(); ++i)
        {
            Impl(m_selection[i])->SetSelectionIndex(static_cast<int>(i), forceRender);
        }

        const bool leadChanged = !(m_lead == lead);
        m_lead = lead;
        if (leadChanged)
        {
            // "kleio --get" without a number returns the selected item (Maccy's Get intent default).
            Controller().SetSelectedItem(m_lead ? Impl(m_lead)->Item() : nullptr);
        }
        if (leadChanged && m_lead)
        {
            Announce(m_lead);   // Maccy NavigationManager.leadHistoryItem didSet
        }
        if (footer != m_footerSelection)
        {
            m_footerSelection = footer;
            UpdateFooterSelection();
        }
        UpdatePasteStackSelection();

        if (scroll && m_lead)
        {
            ScrollIntoView(m_lead);
        }

        // Maccy NavigationManager.leadHistoryItem didSet: restart the preview auto-open timer.
        if (leadChanged || previousCount != m_selection.size())
        {
            if (m_previewOpen)
            {
                UpdatePreview();
            }
            else
            {
                StartAutoOpen();
            }
        }
    }

    void PopupWindow::Announce(ViewModel const& viewModel)
    {
        // Maccy announceForAccessibility (medium priority: rapid selection changes replace each
        // other): a UIA notification from the focused search box; screen readers read it, it's a
        // no-op without one.
        if (!m_shown || !viewModel)
        {
            return;
        }
        const std::wstring text = ::Kleio::PopupText::Truncate(Impl(viewModel)->SelectionAnnouncement(), 200);
        if (text.empty())
        {
            return;
        }
        namespace peers = xaml::Automation::Peers;
        auto peer = peers::FrameworkElementAutomationPeer::FromElement(SearchBox());
        if (!peer)
        {
            peer = peers::FrameworkElementAutomationPeer::CreatePeerForElement(SearchBox());
        }
        if (peer)
        {
            peer.RaiseNotificationEvent(peers::AutomationNotificationKind::ActionCompleted,
                                        peers::AutomationNotificationProcessing::MostRecent,
                                        winrt::hstring{ text }, L"KleioSelectionChanged");
        }
    }

    void PopupWindow::SelectItem(ViewModel const& viewModel, bool scroll)
    {
        if (!viewModel)
        {
            SelectNothing();
            return;
        }
        SetSelection({ viewModel }, viewModel, -1, scroll);
    }

    void PopupWindow::SelectFooter(int index)
    {
        SetSelection({}, nullptr, index, false);
    }

    void PopupWindow::SelectNothing()
    {
        SetSelection({}, nullptr, -1, false);
    }

    void PopupWindow::SelectInitial()
    {
        // Maccy: on activation select the first unpinned item, else the first pinned one.
        if (!m_unpinned.empty())
        {
            SelectItem(m_unpinned.front(), true);
        }
        else if (!m_pinned.empty())
        {
            SelectItem(m_pinned.front(), true);
        }
        else
        {
            SelectNothing();
        }
    }

    void PopupWindow::SelectFirstUnpinned()
    {
        if (!m_unpinned.empty())
        {
            SelectItem(m_unpinned.front(), true);
        }
        else
        {
            SelectNothing();
        }
    }

    void PopupWindow::ScrollIntoView(ViewModel const& viewModel)
    {
        if (std::find(m_unpinned.begin(), m_unpinned.end(), viewModel) != m_unpinned.end())
        {
            HistoryList().ScrollIntoView(viewModel);
        }
    }

    void PopupWindow::KeyboardFirst()
    {
        m_keyboardNavigating = true;
        m_manualMulti = false;
        if (!m_visible.empty())
        {
            SelectItem(m_visible.front(), true);
        }
        else
        {
            SelectNothing();
        }
    }

    void PopupWindow::KeyboardLast()
    {
        m_keyboardNavigating = true;
        m_manualMulti = false;
        const auto footer = FooterItems();
        if (m_lead)
        {
            if (!m_visible.empty() && m_lead == m_visible.back() && !footer.empty())
            {
                SelectFooter(footer.front());
            }
            else if (!m_visible.empty())
            {
                SelectItem(m_visible.back(), true);
            }
        }
        else if (m_footerSelection >= 0)
        {
            if (!footer.empty())
            {
                SelectFooter(footer.back());
            }
        }
        else if (!m_visible.empty())
        {
            SelectItem(m_visible.back(), true);
        }
        else if (!footer.empty())
        {
            SelectFooter(footer.front());
        }
    }

    void PopupWindow::KeyboardNext(bool allowCycle)
    {
        // Maccy NavigationManager.highlightNext(allowCycle:).
        const auto footer = FooterItems();
        if (m_lead)
        {
            m_keyboardNavigating = true;
            m_manualMulti = false;
            const int index = VisibleIndex(m_lead);
            if (index >= 0 && static_cast<std::size_t>(index) + 1 < m_visible.size())
            {
                SelectItem(m_visible[static_cast<std::size_t>(index) + 1], true);
            }
            else if (!footer.empty())
            {
                SelectFooter(footer.front());
            }
            else if (allowCycle)
            {
                KeyboardFirst();
            }
            else if (IsMultiSelect())
            {
                SelectItem(m_lead, true);
            }
        }
        else if (m_footerSelection >= 0)
        {
            m_keyboardNavigating = true;
            m_manualMulti = false;
            auto it = std::find(footer.begin(), footer.end(), m_footerSelection);
            if (it != footer.end() && it + 1 != footer.end())
            {
                SelectFooter(*(it + 1));
            }
            else if (allowCycle)
            {
                // End of the footer while cycling with the popup shortcut: back to the first item
                // (Maccy's intent; its code wraps within the footer before reaching this branch).
                KeyboardFirst();
            }
            else if (!footer.empty())
            {
                SelectFooter(footer.front());   // Maccy: Down on the last footer item wraps to the first
            }
        }
        else if (PasteStackSelected())
        {
            KeyboardFirst();
        }
    }

    void PopupWindow::KeyboardPrevious()
    {
        m_keyboardNavigating = true;
        m_manualMulti = false;
        const auto footer = FooterItems();
        if (m_lead)
        {
            const int index = VisibleIndex(m_lead);
            if (index > 0)
            {
                SelectItem(m_visible[static_cast<std::size_t>(index) - 1], true);
            }
            else if (PasteStackVisible())
            {
                SelectNothing();   // the paste stack becomes the selection
            }
            else
            {
                KeyboardFirst();
            }
        }
        else if (m_footerSelection >= 0)
        {
            auto it = std::find(footer.begin(), footer.end(), m_footerSelection);
            if (it != footer.end() && it != footer.begin())
            {
                SelectFooter(*(it - 1));
            }
            else if (!m_visible.empty())
            {
                SelectItem(m_visible.back(), true);
            }
        }
    }

    void PopupWindow::ExtendNext(bool forward)
    {
        // Maccy extendHighlightToNext / extendHighlightToPrevious.
        if (!m_lead || VisibleIndex(m_lead) < 0)
        {
            forward ? KeyboardNext() : KeyboardPrevious();
            return;
        }
        m_keyboardNavigating = true;
        const int index = VisibleIndex(m_lead);
        const int target = forward ? index + 1 : index - 1;
        if (target < 0 || static_cast<std::size_t>(target) >= m_visible.size())
        {
            return;
        }
        auto to = m_visible[static_cast<std::size_t>(target)];
        auto selection = m_selection;
        if (Impl(to)->IsSelected())
        {
            selection.erase(std::remove(selection.begin(), selection.end(), m_lead), selection.end());
        }
        else
        {
            selection.push_back(to);
        }
        if (selection.empty())
        {
            selection.push_back(to);
        }
        SetSelection(std::move(selection), to, -1, true);
    }

    void PopupWindow::ExtendRange(bool toLast)
    {
        // Maccy extendHighlightToFirst / extendHighlightToLast.
        if (!m_lead || VisibleIndex(m_lead) < 0 || m_visible.empty())
        {
            toLast ? KeyboardLast() : KeyboardFirst();
            return;
        }
        m_keyboardNavigating = true;
        const int from = VisibleIndex(m_lead);
        const int to = toLast ? static_cast<int>(m_visible.size()) - 1 : 0;
        std::vector<ViewModel> selection;
        for (int i = std::min(from, to); i <= std::max(from, to); ++i)
        {
            selection.push_back(m_visible[static_cast<std::size_t>(i)]);
        }
        SetSelection(std::move(selection), m_visible[static_cast<std::size_t>(to)], -1, true);
    }

    void PopupWindow::ToggleInSelection(ViewModel const& viewModel)
    {
        // Maccy NavigationManager.addToSelection.
        auto selection = m_selection;
        ViewModel lead = m_lead;
        if (Impl(viewModel)->IsSelected())
        {
            if (selection.size() <= 1)
            {
                m_manualMulti = !m_manualMulti;
                SetSelection(std::move(selection), lead ? lead : viewModel, -1, false);
                // Badges depend on the multi-selection flag.
                ::Kleio::PopupUiState::Get().multiSelect = IsMultiSelect();
                Impl(viewModel)->NotifyChanged();
                return;
            }
            selection.erase(std::remove(selection.begin(), selection.end(), viewModel), selection.end());
            if (lead == viewModel)
            {
                lead = selection.back();
            }
        }
        else
        {
            selection.push_back(viewModel);
            lead = viewModel;
        }
        SetSelection(std::move(selection), lead, -1, false);
    }

    void PopupWindow::HandleItemClick(IInspectable const& clicked)
    {
        if (!m_shown || m_dialogOpen)
        {
            return;
        }
        auto viewModel = clicked ? clicked.try_as<ViewModel>() : nullptr;
        if (!viewModel)
        {
            return;
        }
        const Modifiers modifiers = ::Kleio::CurrentModifiers() & kModifierMask;
        if (modifiers == Modifiers::Shift)
        {
            // Windows-style multi-selection (Maccy: Cmd-click when multi-selection is enabled).
            ToggleInSelection(viewModel);
            FocusSearch();
            return;
        }
        SelectItem(viewModel, false);
        SelectWithModifiers(Impl(viewModel)->Item(), modifiers);
        if (m_shown)
        {
            FocusSearch();
        }
    }

    bool PopupWindow::PointerReallyMoved(xaml::Input::PointerRoutedEventArgs const& args)
    {
        // Scrolling under a resting mouse must not change the selection (Maccy isKeyboardNavigating):
        // only real mouse movement relative to the window counts.
        const auto position = args.GetCurrentPoint(nullptr).Position();
        if (!m_hasPointerPosition)
        {
            m_hasPointerPosition = true;
            m_pointerPosition = position;
            return false;
        }
        if (std::abs(position.X - m_pointerPosition.X) < 0.5f && std::abs(position.Y - m_pointerPosition.Y) < 0.5f)
        {
            return false;
        }
        m_pointerPosition = position;
        m_keyboardNavigating = false;
        return true;
    }

    void PopupWindow::HandleRowPointerMoved(HistoryItemRow& row, xaml::Input::PointerRoutedEventArgs const& args)
    {
        if (!m_shown || m_dialogOpen || m_flyoutOpen || m_dragInProgress || m_splitterDragging)
        {
            return;
        }
        if (!PointerReallyMoved(args) || IsMultiSelect())
        {
            return;
        }
        if (row.RowMode() == HistoryItemRow::Mode::Static)
        {
            if (!PasteStackSelected())
            {
                SelectNothing();
            }
            return;
        }
        auto viewModel = row.ViewModel();
        if (!viewModel || (m_lead == viewModel && m_selection.size() == 1 && m_footerSelection < 0))
        {
            return;
        }
        SelectItem(viewModel, false);
    }

    void PopupWindow::HandleFooterPointerMoved(int index, xaml::Input::PointerRoutedEventArgs const& args)
    {
        if (!m_shown || m_dialogOpen || m_flyoutOpen || m_dragInProgress || m_splitterDragging)
        {
            return;
        }
        if (!PointerReallyMoved(args))
        {
            return;
        }
        // Maccy FooterItemView.onHover: hovering a footer item closes the preview.
        if (m_previewOpen)
        {
            TogglePreview(true);
        }
        if (IsMultiSelect())
        {
            return;
        }
        if (m_footerSelection != index)
        {
            SelectFooter(index);
        }
    }

    void PopupWindow::HandleRowRightTapped(HistoryItemRow& row, xaml::Input::RightTappedRoutedEventArgs const& args)
    {
        if (!m_shown || m_dialogOpen)
        {
            return;
        }
        auto viewModel = row.ViewModel();
        if (!viewModel)
        {
            return;
        }
        if (!(Impl(viewModel)->IsSelected() && IsMultiSelect()))
        {
            SelectItem(viewModel, false);
        }
        auto target = row.RowRoot();
        ShowItemMenu(viewModel, target, args.GetPosition(target));
        args.Handled(true);
    }

    void PopupWindow::HandlePinnedDragCompleted(controls::DragItemsCompletedEventArgs const& args)
    {
        m_dragInProgress = false;
        RebuildPinnedMirror();
        if (args.DropResult() != winrt::Windows::ApplicationModel::DataTransfer::DataPackageOperation::Move ||
            args.Items().Size() == 0)
        {
            return;
        }
        auto viewModel = args.Items().GetAt(0).try_as<ViewModel>();
        auto* moved = Impl(viewModel);
        if (!moved || !moved->Item())
        {
            return;
        }

        auto const& pinned = Controller().History().PinnedItems();
        const auto from = std::find(pinned.begin(), pinned.end(), moved->Item());
        const auto to = std::find(m_pinnedMirror.begin(), m_pinnedMirror.end(), moved);
        if (from == pinned.end() || to == m_pinnedMirror.end())
        {
            return;
        }
        const auto fromIndex = static_cast<std::size_t>(from - pinned.begin());
        const auto toIndex = static_cast<std::size_t>(to - m_pinnedMirror.begin());
        if (fromIndex != toIndex)
        {
            // Raises History::Changed -> RefreshPreservingSelection (the list already matches).
            Controller().History().MovePin(fromIndex, toIndex);
        }
        FocusSearch();
    }

    // =====================================================================================
    // Keyboard
    // =====================================================================================

    void PopupWindow::HandlePreviewKeyDown(IInspectable const&, xaml::Input::KeyRoutedEventArgs const& args)
    {
        Guard([&] {
            if (m_dialogOpen || !m_shown)
            {
                return;
            }
            const auto key = static_cast<uint32_t>(args.Key());
            if (key == VK_PROCESSKEY)
            {
                return;   // IME composition
            }
            if (IsModifierKey(key))
            {
                UpdateModifiers();
                return;
            }
            const Modifiers modifiers = ::Kleio::CurrentModifiers() & kModifierMask;
            SetModifiers(modifiers);
            if (HandleKey(key, modifiers))
            {
                args.Handled(true);
            }
        });
    }

    void PopupWindow::HandlePreviewKeyUp(IInspectable const&, xaml::Input::KeyRoutedEventArgs const& args)
    {
        Guard([&] {
            if (IsModifierKey(static_cast<uint32_t>(args.Key())))
            {
                UpdateModifiers();
                CheckShortcutModifiers();
            }
        });
    }

    void PopupWindow::UpdateModifiers()
    {
        SetModifiers(::Kleio::CurrentModifiers() & kModifierMask);
    }

    void PopupWindow::SetModifiers(Modifiers modifiers)
    {
        auto& ui = ::Kleio::PopupUiState::Get();
        if (ui.modifiers == modifiers)
        {
            return;
        }
        ui.modifiers = modifiers;
        // Shortcut hints follow the held modifiers (Maccy KeyShortcut.isVisible).
        for (auto const& viewModel : m_pinned)
        {
            Impl(viewModel)->NotifyChanged();
        }
        for (std::size_t i = 0; i < m_unpinned.size() && i < kShortcutRows; ++i)
        {
            Impl(m_unpinned[i])->NotifyChanged();
        }
        UpdateClearAll();
    }

    void PopupWindow::UpdateClearAll()
    {
        // Maccy FooterView: "Clear" turns into "Clear all" while Shift is held.
        const bool clearAll = ::Kleio::HasFlag(::Kleio::PopupUiState::Get().modifiers, Modifiers::Shift);
        if (clearAll == m_showClearAll)
        {
            return;
        }
        m_showClearAll = clearAll;
        FooterClearRow().Visibility(VisibleIf(!clearAll));
        FooterClearAllRow().Visibility(VisibleIf(clearAll));
        if (clearAll && m_footerSelection == 0)
        {
            SelectFooter(1);
        }
        else if (!clearAll && m_footerSelection == 1)
        {
            SelectFooter(0);
        }
    }

    bool PopupWindow::HandleKey(uint32_t key, Modifiers modifiers)
    {
        auto& settings = Controller().Settings();
        const auto pressed = MakeShortcut(modifiers, key);
        auto matches = [&](::Kleio::KeyShortcut const& shortcut) { return !shortcut.IsEmpty() && shortcut == pressed; };

        // Maccy KeyChord.clearHistory / clearHistoryAll.
        if (key == VK_BACK && modifiers == (Modifiers::Ctrl | Modifiers::Alt))
        {
            RequestClear(false);
            return true;
        }
        if (key == VK_BACK && modifiers == kModifierMask)
        {
            RequestClear(true);
            return true;
        }
        // Maccy KeyChord.clearSearch (Control+U).
        if (key == 'U' && modifiers == Modifiers::Ctrl)
        {
            SetSearchQuery(L"");
            return true;
        }
        if (matches(settings.DeleteShortcut()))
        {
            if (PasteStackSelected())
            {
                StopPasteStack();
            }
            else
            {
                DeleteSelection();
            }
            return true;
        }

        // Maccy KeyChord Emacs-style keys (Control -> Ctrl, Option -> Alt). Their letters are never
        // pin letters (History::AvailablePins), so they don't shadow pinned items.
        if (key == 'H' && modifiers == Modifiers::Ctrl)
        {
            // deleteOneCharFromSearch (whole query, like Maccy's searchQuery.popLast()).
            FocusSearch();
            std::wstring query{ SearchBox().Text() };
            if (!query.empty())
            {
                const bool pair = query.size() >= 2 && IS_LOW_SURROGATE(query.back()) && IS_HIGH_SURROGATE(query[query.size() - 2]);
                query.resize(query.size() - (pair ? 2 : 1));
                SetSearchQuery(query);
            }
            return true;
        }
        if (key == 'W' && modifiers == Modifiers::Ctrl)
        {
            // deleteLastWordFromSearch.
            FocusSearch();
            SetSearchQuery(DropLastWord(std::wstring{ SearchBox().Text() }));
            return true;
        }
        const bool ctrlAlt = ::Kleio::HasFlag(modifiers, Modifiers::Ctrl) && ::Kleio::HasFlag(modifiers, Modifiers::Alt);
        if ((key == 'N' || key == 'P') && !(ctrlAlt && IsAltGrCharacter(key)))
        {
            const bool next = key == 'N';
            if (modifiers == Modifiers::Ctrl)
            {
                next ? KeyboardNext() : KeyboardPrevious();               // moveToNext / moveToPrevious
                return true;
            }
            if (modifiers == (Modifiers::Ctrl | Modifiers::Shift))
            {
                ExtendNext(next);                                         // extendToNext / extendToPrevious
                return true;
            }
            if (modifiers == (Modifiers::Ctrl | Modifiers::Alt))
            {
                next ? KeyboardLast() : KeyboardFirst();                  // moveToLast / moveToFirst
                return true;
            }
            if (modifiers == kModifierMask)
            {
                ExtendRange(next);                                        // extendToLast / extendToFirst
                return true;
            }
        }
        if (key == 'J' && modifiers == Modifiers::Ctrl)
        {
            KeyboardNext();
            return true;
        }
        if (key == 'K' && modifiers == Modifiers::Ctrl)
        {
            // Maccy issue #1055: on the first item Ctrl+K is left to the search field.
            const bool firstHighlighted = m_visible.empty() ? !m_lead : m_lead == m_visible.front();
            if (!firstHighlighted)
            {
                KeyboardPrevious();
                return true;
            }
            return false;
        }

        if (matches(settings.PinShortcut()))
        {
            TogglePinSelection();
            return true;
        }
        if (matches(settings.TogglePreviewShortcut()))
        {
            TogglePreview(true);
            return true;
        }

        const bool shift = ::Kleio::HasFlag(modifiers, Modifiers::Shift);
        const bool ctrlOrAlt = ::Kleio::HasFlag(modifiers, Modifiers::Ctrl) || ::Kleio::HasFlag(modifiers, Modifiers::Alt);
        switch (key)
        {
        case VK_DOWN:
            if (modifiers == Modifiers::Shift)
            {
                ExtendNext(true);
            }
            else if (shift && ctrlOrAlt)
            {
                ExtendRange(true);
            }
            else if (ctrlOrAlt)
            {
                KeyboardLast();
            }
            else
            {
                KeyboardNext();
            }
            return true;
        case VK_UP:
            if (modifiers == Modifiers::Shift)
            {
                ExtendNext(false);
            }
            else if (shift && ctrlOrAlt)
            {
                ExtendRange(false);
            }
            else if (ctrlOrAlt)
            {
                KeyboardFirst();
            }
            else
            {
                KeyboardPrevious();
            }
            return true;
        case VK_NEXT:
            shift ? ExtendRange(true) : KeyboardLast();
            return true;
        case VK_PRIOR:
            shift ? ExtendRange(false) : KeyboardFirst();
            return true;
        case VK_HOME:
        case VK_END:
            // With a query Home/End move the caret, unless Ctrl is held.
            if (m_query.empty() || ::Kleio::HasFlag(modifiers, Modifiers::Ctrl))
            {
                const bool last = key == VK_END;
                if (shift)
                {
                    ExtendRange(last);
                }
                else
                {
                    last ? KeyboardLast() : KeyboardFirst();
                }
                return true;
            }
            return false;
        case VK_RETURN:
            ActivateSelection(modifiers);
            return true;
        case VK_ESCAPE:
            RequestHide();   // Maccy KeyChord.close
            return true;
        case VK_APPS:
            OpenContextMenuForLead();
            return true;
        case VK_F10:
            if (modifiers == Modifiers::Shift)
            {
                OpenContextMenuForLead();
                return true;
            }
            break;
        default:
            break;
        }

        if (key == VK_OEM_COMMA && modifiers == Modifiers::Ctrl)
        {
            Controller().OpenSettings();
            return true;
        }
        if (key == 'Q' && modifiers == Modifiers::Ctrl)
        {
            Controller().Quit();
            return true;
        }

        // Maccy History.pressedShortcutItem: Ctrl/Alt(+Shift) + 1..9 or + pin letter.
        if (auto viewModel = PressedShortcutItem(key, modifiers))
        {
            SelectItem(viewModel, true);
            SelectWithModifiers(Impl(viewModel)->Item(), modifiers);
            return true;
        }
        return false;
    }

    winrt::Kleio::HistoryItemViewModel PopupWindow::PressedShortcutItem(uint32_t key, Modifiers modifiers) const
    {
        auto const& ui = ::Kleio::PopupUiState::Get();
        if (modifiers == Modifiers::None ||
            ::Kleio::ActionForModifiers(modifiers, ui.pasteByDefault, ui.removeFormattingByDefault) == ::Kleio::ItemAction::Unknown)
        {
            return nullptr;
        }
        std::wstring shortcutKey;
        if (key >= '1' && key <= '9')
        {
            shortcutKey.assign(1, static_cast<wchar_t>(key));
        }
        else if (key >= VK_NUMPAD1 && key <= VK_NUMPAD9)
        {
            shortcutKey.assign(1, static_cast<wchar_t>(L'1' + (key - VK_NUMPAD1)));
        }
        else if (key >= 'A' && key <= 'Z')
        {
            shortcutKey.assign(1, static_cast<wchar_t>(L'a' + (key - 'A')));
        }
        if (shortcutKey.empty())
        {
            return nullptr;
        }
        for (auto const& viewModel : m_visible)
        {
            if (Impl(viewModel)->ShortcutKey() == shortcutKey)
            {
                return viewModel;
            }
        }
        return nullptr;
    }

    void PopupWindow::SetSearchQuery(std::wstring const& text)
    {
        // Maccy assigns searchQuery; the caret ends up after the text.
        if (std::wstring{ SearchBox().Text() } != text || m_query != text)
        {
            SetQueryNow(text);
        }
        SearchBox().Select(static_cast<int32_t>(text.size()), 0);
    }

    // =====================================================================================
    // Actions
    // =====================================================================================

    void PopupWindow::SelectWithModifiers(::Kleio::HistoryItemPtr const& item, Modifiers modifiers)
    {
        if (item)
        {
            // Hides the popup, copies and (depending on modifiers/settings) pastes.
            Controller().Select(item, modifiers);
        }
    }

    void PopupWindow::ActivateSelection(Modifiers modifiers)
    {
        // Maccy AppState.select.
        if (IsMultiSelect() && !m_selection.empty())
        {
            std::vector<::Kleio::HistoryItemPtr> items;
            for (auto const& viewModel : m_selection)
            {
                if (auto const& item = Impl(viewModel)->Item())
                {
                    items.push_back(item);
                }
            }
            m_manualMulti = false;
            Controller().StartPasteStack(std::move(items), modifiers);
            return;
        }
        if (m_lead)
        {
            SelectWithModifiers(Impl(m_lead)->Item(), modifiers);
            return;
        }
        if (m_footerSelection >= 0)
        {
            ActivateFooter(m_footerSelection);
            return;
        }
        if (!m_query.empty())
        {
            // Nothing matched: copy the query itself.
            Controller().Clipboard().CopyText(m_query);
            RequestHide();
        }
    }

    void PopupWindow::ActivateFooter(int index)
    {
        switch (index)
        {
        case 0:
            RequestClear(false);
            break;
        case 1:
            RequestClear(true);
            break;
        case 2:
            Controller().OpenSettings();
            break;
        case 3:
            Controller().OpenAbout();
            break;
        case 4:
            Controller().Quit();
            break;
        default:
            break;
        }
    }

    void PopupWindow::DeleteSelection()
    {
        // Maccy AppState.deleteSelection.
        if (!m_lead || m_selection.empty())
        {
            return;
        }
        const int index = VisibleIndex(m_lead);
        ViewModel next{ nullptr };
        if (index >= 0)
        {
            for (std::size_t i = static_cast<std::size_t>(index) + 1; i < m_visible.size() && !next; ++i)
            {
                if (!Impl(m_visible[i])->IsSelected())
                {
                    next = m_visible[i];
                }
            }
            for (int i = index - 1; i >= 0 && !next; --i)
            {
                if (!Impl(m_visible[static_cast<std::size_t>(i)])->IsSelected())
                {
                    next = m_visible[static_cast<std::size_t>(i)];
                }
            }
        }

        std::vector<::Kleio::HistoryItemPtr> items;
        for (auto const& viewModel : m_selection)
        {
            if (auto const& item = Impl(viewModel)->Item())
            {
                items.push_back(item);
            }
        }
        if (next)
        {
            m_pendingSelectId = Impl(next)->Id();
        }

        ++m_batchDepth;
        for (auto const& item : items)
        {
            try
            {
                Controller().History().Delete(item);
            }
            catch (...)
            {
                Log(L"Kleio popup: delete failed");
            }
        }
        --m_batchDepth;
        m_refreshPending = false;
        m_keyboardNavigating = true;
        m_manualMulti = false;
        RefreshPreservingSelection();
    }

    void PopupWindow::TogglePinSelection()
    {
        // Maccy AppState.togglePin (+ History.togglePin clears the search).
        if (m_selection.empty())
        {
            return;
        }
        std::vector<::Kleio::HistoryItemPtr> items;
        for (auto const& viewModel : m_selection)
        {
            if (auto const& item = Impl(viewModel)->Item())
            {
                items.push_back(item);
            }
        }
        ++m_batchDepth;
        for (auto const& item : items)
        {
            try
            {
                Controller().History().TogglePin(item);
            }
            catch (...)
            {
                Log(L"Kleio popup: toggling the pin failed");
            }
        }
        --m_batchDepth;
        m_refreshPending = false;

        if (!m_query.empty() || !SearchBox().Text().empty())
        {
            m_searchTimer.Stop();
            m_query.clear();
            SearchBox().Text(L"");
            PinnedList().CanReorderItems(true);
            PinnedList().CanDragItems(true);
        }
        RefreshPreservingSelection();
        if (m_lead)
        {
            ScrollIntoView(m_lead);
        }
    }

    void PopupWindow::StopPasteStack()
    {
        Controller().InterruptPasteStack();
        UpdatePasteStack();
        UpdateChrome();
        if (!m_lead && m_footerSelection < 0)
        {
            KeyboardFirst();
        }
        FitHeight();
    }

    void PopupWindow::CopyRecognizedText()
    {
        if (!m_lead)
        {
            return;
        }
        auto const& item = Impl(m_lead)->Item();
        if (item && item->IsImage() && !item->title.empty())
        {
            Controller().Clipboard().CopyText(item->title);
        }
    }

    void PopupWindow::OpenContextMenuForLead()
    {
        if (!m_lead)
        {
            return;
        }
        xaml::UIElement target{ nullptr };
        if (auto container = HistoryList().ContainerFromItem(m_lead))
        {
            target = container.try_as<xaml::UIElement>();
        }
        else if (auto pinnedContainer = PinnedList().ContainerFromItem(m_lead))
        {
            target = pinnedContainer.try_as<xaml::UIElement>();
        }
        if (!target)
        {
            target = SearchBox();
        }
        ShowItemMenu(m_lead, target, winrt::Windows::Foundation::Point{ 24.0f, 16.0f });
    }

    void PopupWindow::ShowItemMenu(ViewModel const& viewModel, xaml::UIElement const& target, winrt::Windows::Foundation::Point const& position)
    {
        // Windows-guideline addition: a context menu with the item actions.
        auto* impl = Impl(viewModel);
        if (!impl || !impl->Item())
        {
            return;
        }
        auto item = impl->Item();
        auto& ui = ::Kleio::PopupUiState::Get();
        auto& settings = Controller().Settings();
        auto weak = get_weak();

        controls::MenuFlyout menu;
        auto add = [&](std::wstring const& text, wchar_t const* glyph, std::wstring const& accelerator, std::function<void(PopupWindow&)> action) {
            controls::MenuFlyoutItem entry;
            entry.Text(winrt::hstring{ text });
            if (glyph)
            {
                controls::FontIcon icon;
                icon.Glyph(glyph);
                entry.Icon(icon);
            }
            if (!accelerator.empty())
            {
                entry.KeyboardAcceleratorTextOverride(winrt::hstring{ accelerator });
            }
            entry.Click([weak, action](auto&&, auto&&) {
                if (auto self = weak.get())
                {
                    Guard([&] { action(*self); });
                }
            });
            menu.Items().Append(entry);
        };
        auto enterHint = [](Modifiers modifiers) { return MakeShortcut(modifiers, VK_RETURN).ToString(); };

        const Modifiers copyModifiers = ui.ModifiersFor(::Kleio::ItemAction::Copy);
        const Modifiers pasteModifiers = ui.ModifiersFor(::Kleio::ItemAction::Paste);
        const Modifiers plainModifiers = ui.ModifiersFor(::Kleio::ItemAction::PasteWithoutFormatting);
        if (copyModifiers != Modifiers::None)
        {
            add(L"Copy", L"\uE8C8", enterHint(copyModifiers), [item, copyModifiers](PopupWindow& self) { self.SelectWithModifiers(item, copyModifiers); });
        }
        if (pasteModifiers != Modifiers::None)
        {
            add(L"Paste", L"\uE77F", enterHint(pasteModifiers), [item, pasteModifiers](PopupWindow& self) { self.SelectWithModifiers(item, pasteModifiers); });
        }
        if (plainModifiers != Modifiers::None)
        {
            add(L"Paste without formatting", nullptr, enterHint(plainModifiers), [item, plainModifiers](PopupWindow& self) { self.SelectWithModifiers(item, plainModifiers); });
        }
        menu.Items().Append(controls::MenuFlyoutSeparator{});
        add(item->IsPinned() ? L"Unpin" : L"Pin", item->IsPinned() ? L"\uE77A" : L"\uE718", ShortcutText(settings.PinShortcut()),
            [](PopupWindow& self) { self.TogglePinSelection(); });
        if (!item->IsImage())
        {
            add(L"Edit\u2026", L"\uE70F", L"", [viewModel](PopupWindow& self) { self.OpenEditor(viewModel); });
        }
        add(L"Delete", L"\uE74D", ShortcutText(settings.DeleteShortcut()), [](PopupWindow& self) { self.DeleteSelection(); });

        menu.Closed([weak](auto&&, auto&&) {
            if (auto self = weak.get())
            {
                self->m_flyoutOpen = false;
                // Let the clicked item's action run first, then check where the focus went.
                self->DispatcherQueue().TryEnqueue([weak]() {
                    if (auto popup = weak.get())
                    {
                        Guard([&] { popup->AfterTransientUi(); });
                    }
                });
            }
        });

        m_flyoutOpen = true;
        m_previewTimer.Stop();
        try
        {
            menu.ShowAt(target, position);
        }
        catch (...)
        {
            m_flyoutOpen = false;
            throw;
        }
    }

    void PopupWindow::AfterTransientUi()
    {
        if (!m_shown || m_dialogOpen || m_flyoutOpen)
        {
            return;
        }
        if (m_hwnd && GetForegroundWindow() != m_hwnd)
        {
            // The user went elsewhere while a menu/dialog of the popup was open.
            RequestHide();
            return;
        }
        FocusSearch();
        StartAutoOpen();
    }

    void PopupWindow::PrepareDialog(controls::ContentDialog const& dialog)
    {
        dialog.XamlRoot(Content().XamlRoot());
        try
        {
            auto style = xaml::Application::Current().Resources().Lookup(winrt::box_value(L"DefaultContentDialogStyle"));
            if (auto typed = style.try_as<xaml::Style>())
            {
                dialog.Style(typed);
            }
        }
        catch (...)
        {
        }
    }

    void PopupWindow::BeginDialog(double minWidth, double minHeight)
    {
        m_dialogOpen = true;
        m_previewTimer.Stop();
        m_rectBeforeDialog.reset();

        // Make room for the dialog when the popup is small.
        RECT client{};
        if (!GetClientScreenRect(client))
        {
            return;
        }
        const int width = client.right - client.left;
        const int height = client.bottom - client.top;
        const int minWidthPx = static_cast<int>(std::lround(minWidth * m_scale));
        const int minHeightPx = static_cast<int>(std::lround(minHeight * m_scale));
        if (width >= minWidthPx && height >= minHeightPx)
        {
            return;
        }
        const RECT work = WorkArea(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST));
        const int newWidth = std::min<int>(std::max(width, minWidthPx), work.right - work.left);
        const int newHeight = std::min<int>(std::max(height, minHeightPx), work.bottom - work.top);
        const int x = ClampInt(client.left, work.left, work.right - newWidth);
        const int y = ClampInt(m_anchorBottom ? client.bottom - newHeight : client.top, work.top, work.bottom - newHeight);
        m_rectBeforeDialog = client;
        SetClientRect(x, y, newWidth, newHeight);
    }

    void PopupWindow::EndDialog()
    {
        m_dialogOpen = false;
        if (m_rectBeforeDialog)
        {
            const RECT previous = *m_rectBeforeDialog;
            m_rectBeforeDialog.reset();
            if (m_shown)
            {
                SetClientRect(previous.left, previous.top, previous.right - previous.left, previous.bottom - previous.top);
            }
        }
    }

    winrt::fire_and_forget PopupWindow::RequestClear(bool all)
    {
        auto strong = get_strong();
        try
        {
            auto& settings = Controller().Settings();
            if (settings.SuppressClearAlert())
            {
                all ? Controller().ClearAll() : Controller().Clear();
                co_return;
            }
            if (m_dialogOpen || !m_shown)
            {
                co_return;
            }

            // Maccy ConfirmationView with clear_alert_* strings and the dialog suppression toggle.
            controls::ContentDialog dialog;
            PrepareDialog(dialog);
            dialog.Title(winrt::box_value(winrt::hstring{ all ? L"Clear all history?" : L"Clear history?" }));

            controls::StackPanel panel;
            panel.Spacing(12);
            controls::TextBlock message;
            message.TextWrapping(xaml::TextWrapping::Wrap);
            message.Text(all ? L"Are you sure you want to clear the history, including pinned items? You can't undo this action."
                             : L"Are you sure you want to clear the history? Pinned items are kept. You can't undo this action.");
            controls::CheckBox dontAsk;
            dontAsk.Content(winrt::box_value(winrt::hstring{ L"Don't ask again" }));
            panel.Children().Append(message);
            panel.Children().Append(dontAsk);
            dialog.Content(panel);
            dialog.PrimaryButtonText(all ? L"Clear all" : L"Clear");
            dialog.CloseButtonText(L"Cancel");
            dialog.DefaultButton(controls::ContentDialogButton::Primary);

            BeginDialog(400, 300);
            controls::ContentDialogResult result = controls::ContentDialogResult::None;
            try
            {
                result = co_await dialog.ShowAsync();
            }
            catch (...)
            {
                result = controls::ContentDialogResult::None;
            }
            EndDialog();

            if (result == controls::ContentDialogResult::Primary)
            {
                auto checked = dontAsk.IsChecked();
                if (checked && checked.Value())
                {
                    settings.SuppressClearAlert(true);
                }
                all ? Controller().ClearAll() : Controller().Clear();
            }
            AfterTransientUi();
        }
        catch (...)
        {
            m_dialogOpen = false;
            Log(L"Kleio popup: clear confirmation failed");
        }
    }

    winrt::fire_and_forget PopupWindow::OpenEditor(ViewModel viewModel)
    {
        // Maccy ItemEditorView: alias (title), pin key and the plain text content.
        auto strong = get_strong();
        try
        {
            if (m_dialogOpen || !m_shown || !viewModel)
            {
                co_return;
            }
            auto item = Impl(viewModel)->Item();
            if (!item)
            {
                co_return;
            }
            auto& history = Controller().History();

            const bool isImage = item->IsImage();
            const bool hasFiles = !item->Files().empty();
            const auto text = item->Text();
            const bool textContent = text.has_value() && !isImage && !hasFiles;
            const bool richText = item->Html().has_value() || item->Rtf().has_value();
            const bool tooLarge = textContent && text->size() > kEditorTextLimit;
            const bool editable = textContent && !tooLarge;
            const std::wstring originalTitle = item->title;
            const std::wstring originalPin = item->pin;
            const std::wstring originalText = textContent ? NormalizeNewlines(*text) : std::wstring{};
            const bool crlf = textContent && text->find(L"\r\n") != std::wstring::npos;

            controls::StackPanel panel;
            panel.Spacing(12);
            panel.MinWidth(320);

            controls::TextBox alias;
            alias.Header(winrt::box_value(winrt::hstring{ L"Alias" }));
            alias.Text(winrt::hstring{ originalTitle });
            alias.IsEnabled(!isImage);
            alias.IsSpellCheckEnabled(false);
            panel.Children().Append(alias);

            controls::ComboBox keyBox{ nullptr };
            std::vector<std::wstring> pins;
            if (item->IsPinned())
            {
                pins = history.AvailablePins();
                pins.push_back(originalPin);
                std::sort(pins.begin(), pins.end());
                pins.erase(std::unique(pins.begin(), pins.end()), pins.end());
                keyBox = controls::ComboBox{};
                keyBox.Header(winrt::box_value(winrt::hstring{ L"Key" }));
                int selectedIndex = -1;
                for (std::size_t i = 0; i < pins.size(); ++i)
                {
                    keyBox.Items().Append(winrt::box_value(winrt::hstring{ pins[i] }));
                    if (pins[i] == originalPin)
                    {
                        selectedIndex = static_cast<int>(i);
                    }
                }
                keyBox.SelectedIndex(selectedIndex);
                panel.Children().Append(keyBox);
            }

            controls::TextBox content;
            content.Header(winrt::box_value(winrt::hstring{ L"Content" }));
            content.AcceptsReturn(true);
            content.TextWrapping(xaml::TextWrapping::Wrap);
            content.IsSpellCheckEnabled(false);
            content.Height(220);
            controls::ScrollViewer::SetVerticalScrollBarVisibility(content, controls::ScrollBarVisibility::Auto);
            if (editable)
            {
                content.Text(winrt::hstring{ *text });
            }
            else
            {
                content.Text(tooLarge ? L"This text is too long to edit." : L"The content of this item is not text and can't be edited.");
                content.IsEnabled(false);
            }
            panel.Children().Append(content);

            if (editable && richText)
            {
                controls::TextBlock warning;
                warning.TextWrapping(xaml::TextWrapping::Wrap);
                warning.Text(L"Saving changed content keeps only the plain text; the formatting is removed.");
                warning.Opacity(0.7);
                panel.Children().Append(warning);
            }

            controls::ContentDialog dialog;
            PrepareDialog(dialog);
            dialog.Title(winrt::box_value(winrt::hstring{ L"Edit item" }));
            dialog.Content(panel);
            dialog.PrimaryButtonText(L"Save");
            dialog.CloseButtonText(L"Cancel");
            dialog.DefaultButton(controls::ContentDialogButton::Primary);

            BeginDialog(480, 520);
            controls::ContentDialogResult result = controls::ContentDialogResult::None;
            try
            {
                result = co_await dialog.ShowAsync();
            }
            catch (...)
            {
                result = controls::ContentDialogResult::None;
            }
            EndDialog();

            if (result == controls::ContentDialogResult::Primary && history.FindById(item->id) == item)
            {
                ++m_batchDepth;
                try
                {
                    if (editable)
                    {
                        const std::wstring edited = NormalizeNewlines(std::wstring_view{ content.Text() });
                        if (edited != originalText)
                        {
                            history.UpdateText(item, WithNewlines(edited, crlf));
                        }
                    }
                    if (!isImage)
                    {
                        const std::wstring newTitle{ alias.Text() };
                        if (newTitle != originalTitle && !newTitle.empty())
                        {
                            history.UpdateTitle(item, newTitle);
                        }
                    }
                    if (keyBox)
                    {
                        const int index = keyBox.SelectedIndex();
                        if (index >= 0 && static_cast<std::size_t>(index) < pins.size() && pins[static_cast<std::size_t>(index)] != originalPin)
                        {
                            history.UpdatePin(item, pins[static_cast<std::size_t>(index)]);
                        }
                    }
                }
                catch (...)
                {
                    Log(L"Kleio popup: saving the edited item failed");
                }
                --m_batchDepth;
                m_refreshPending = false;
                if (m_shown)
                {
                    RefreshPreservingSelection();
                }
            }
            AfterTransientUi();
        }
        catch (...)
        {
            m_dialogOpen = false;
            Log(L"Kleio popup: item editor failed");
        }
    }

    // =====================================================================================
    // Preview
    // =====================================================================================

    void PopupWindow::ApplyPreviewLayout()
    {
        const auto visibility = VisibleIf(m_previewOpen);
        PreviewPanel().Visibility(visibility);
        PreviewSplitter().Visibility(visibility);
        PreviewPanel().Width(std::max(kMinPreviewWidth, Controller().Settings().PreviewWidth()));

        const xaml::GridLength star{ 1.0, xaml::GridUnitType::Star };
        const xaml::GridLength automatic{ 1.0, xaml::GridUnitType::Auto };
        if (m_placementLeft)
        {
            controls::Grid::SetColumn(PreviewPanel(), 0);
            controls::Grid::SetColumn(PreviewSplitter(), 1);
            controls::Grid::SetColumn(ContentPanel(), 2);
            LayoutColumn0().Width(automatic);
            LayoutColumn2().Width(star);
        }
        else
        {
            controls::Grid::SetColumn(ContentPanel(), 0);
            controls::Grid::SetColumn(PreviewSplitter(), 1);
            controls::Grid::SetColumn(PreviewPanel(), 2);
            LayoutColumn0().Width(star);
            LayoutColumn2().Width(automatic);
        }
        // Sidebar glyph pointing at where the preview opens.
        PreviewToggleIcon().Glyph(m_placementLeft ? L"\uE90C" : L"\uE90D");
        UpdatePreviewToggleTooltip();
    }

    void PopupWindow::TogglePreview(bool manual)
    {
        // Maccy SlideoutController.togglePreview.
        if (!m_previewOpen && !m_lead && !PasteStackSelected())
        {
            return;
        }
        if (manual)
        {
            // Closing by hand suppresses auto-open until the popup is opened again.
            m_autoOpenSuppressed = m_previewOpen;
        }
        m_previewTimer.Stop();

        auto& settings = Controller().Settings();
        const bool opening = !m_previewOpen;
        const int previewPx = static_cast<int>(std::lround((std::max(kMinPreviewWidth, settings.PreviewWidth()) + kSplitterWidth) * m_scale));
        RECT client{};
        const bool haveRect = m_shown && GetClientScreenRect(client);
        const RECT work = WorkArea(m_hwnd ? MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST) : nullptr);

        if (opening && haveRect)
        {
            // Open to the left when there is no room on the right (Maccy computePlacement).
            m_placementLeft = client.right + previewPx > work.right && client.left - previewPx >= work.left;
        }
        m_previewOpen = opening;
        ApplyPreviewLayout();
        if (opening)
        {
            m_previewItemId = 0;
            UpdatePreview();
        }

        if (haveRect)
        {
            int x = client.left;
            int width = client.right - client.left;
            if (opening)
            {
                width += previewPx;
                if (m_placementLeft)
                {
                    x -= previewPx;
                }
            }
            else
            {
                width = std::max(static_cast<int>(std::lround(kMinContentWidth * m_scale)), width - previewPx);
                if (m_placementLeft)
                {
                    x += previewPx;
                }
            }
            width = std::min<int>(width, work.right - work.left);
            x = ClampInt(x, work.left, work.right - width);
            SetClientRect(x, client.top, width, client.bottom - client.top);
            FitHeight();
        }
        if (!opening)
        {
            m_placementLeft = false;
            ApplyPreviewLayout();
        }
    }

    void PopupWindow::UpdatePreview()
    {
        if (!m_previewOpen)
        {
            return;
        }
        auto& settings = Controller().Settings();
        const bool stackSelected = PasteStackSelected();
        const bool hasSelection = !m_selection.empty();

        // Toolbar (Maccy ToolbarView).
        PreviewPinButton().Visibility(VisibleIf(hasSelection));
        PreviewEditButton().Visibility(VisibleIf(hasSelection));
        PreviewDeleteButton().Visibility(VisibleIf(hasSelection));
        PreviewStopStackButton().Visibility(VisibleIf(stackSelected));

        bool allPinned = hasSelection;
        bool anyPinned = false;
        for (auto const& viewModel : m_selection)
        {
            allPinned = allPinned && Impl(viewModel)->IsPinned();
            anyPinned = anyPinned || Impl(viewModel)->IsPinned();
        }
        PreviewPinIcon().Glyph(allPinned ? L"\uE77A" : L"\uE718");
        PreviewPinButton().IsEnabled(!(anyPinned && !allPinned));
        const auto pinShortcut = ShortcutText(settings.PinShortcut());
        const auto deleteShortcut = ShortcutText(settings.DeleteShortcut());
        SetToolTip(PreviewPinButton(), pinShortcut.empty() ? std::wstring{ allPinned ? L"Unpin" : L"Pin" }
                                                           : L"Press " + pinShortcut + (allPinned ? L" to unpin." : L" to pin."));
        SetToolTip(PreviewDeleteButton(), deleteShortcut.empty() ? std::wstring{ L"Delete" } : L"Press " + deleteShortcut + L" to delete.");

        ::Kleio::HistoryItemPtr item = m_lead ? Impl(m_lead)->Item() : nullptr;
        PreviewEditButton().IsEnabled(m_selection.size() == 1 && item && !item->IsImage());
        PreviewCopyTextButton().Visibility(VisibleIf(m_selection.size() == 1 && item && item->IsImage() && !item->title.empty()));

        const std::int64_t itemId = item ? item->id : (stackSelected ? -1 : 0);
        const bool itemChanged = itemId != m_previewItemId;
        m_previewItemId = itemId;

        if (item)
        {
            if (item->IsImage())
            {
                const auto pixels = Impl(m_lead)->ImagePixelSize();
                if (m_previewImageId != item->id || !m_previewBitmap)
                {
                    if (auto png = item->Image())
                    {
                        m_previewBitmap = ::Kleio::PopupImages::FromPng(*png, 0, pixels.width > 2048 ? 2048 : 0);
                        m_previewImageId = item->id;
                    }
                }
                PreviewImage().Source(m_previewBitmap);
                if (!pixels.IsEmpty())
                {
                    PreviewImage().MaxWidth(pixels.width / m_scale);
                    PreviewImage().MaxHeight(pixels.height / m_scale);
                    PreviewDimensions().Text(L"Dimensions: " + std::to_wstring(pixels.width) + L"\u00D7" + std::to_wstring(pixels.height));
                }
                PreviewDimensions().Visibility(VisibleIf(!pixels.IsEmpty()));
                PreviewImage().Visibility(xaml::Visibility::Visible);
                PreviewText().Visibility(xaml::Visibility::Collapsed);
                PreviewText().Text(L"");
            }
            else
            {
                PreviewImage().Source(nullptr);
                PreviewImage().Visibility(xaml::Visibility::Collapsed);
                PreviewDimensions().Visibility(xaml::Visibility::Collapsed);
                PreviewText().Visibility(xaml::Visibility::Visible);
                if (itemChanged || PreviewText().Text().empty())
                {
                    PreviewText().Text(winrt::hstring{ ::Kleio::PopupText::Truncate(item->PreviewableText(), kPreviewTextLimit) });
                }
            }

            if (!item->application.empty())
            {
                PreviewAppName().Text(winrt::hstring{ ::Kleio::PopupImages::AppName(item->application) });
                PreviewAppIcon().Source(::Kleio::PopupImages::AppIcon(item->application, m_scale));
                PreviewAppRow().Visibility(xaml::Visibility::Visible);
            }
            else
            {
                PreviewAppRow().Visibility(xaml::Visibility::Collapsed);
            }
            PreviewFirstCopy().Text(L"First copy time: " + ::Kleio::PopupText::FormatTimestamp(item->firstCopiedAt));
            PreviewLastCopy().Text(L"Last copy time: " + ::Kleio::PopupText::FormatTimestamp(item->lastCopiedAt));
            PreviewCopies().Text(L"Number of copies: " + std::to_wstring(item->numberOfCopies));

            if (pinShortcut.empty())
            {
                PreviewPinHint().Visibility(xaml::Visibility::Collapsed);
            }
            else
            {
                PreviewPinHint().Text(L"Press " + pinShortcut + (item->IsPinned() ? L" to unpin." : L" to pin."));
                PreviewPinHint().Visibility(xaml::Visibility::Visible);
            }
            if (deleteShortcut.empty())
            {
                PreviewDeleteHint().Visibility(xaml::Visibility::Collapsed);
            }
            else
            {
                PreviewDeleteHint().Text(L"Press " + deleteShortcut + L" to delete.");
                PreviewDeleteHint().Visibility(xaml::Visibility::Visible);
            }
            PreviewInfo().Visibility(xaml::Visibility::Visible);
        }
        else
        {
            PreviewImage().Source(nullptr);
            PreviewImage().Visibility(xaml::Visibility::Collapsed);
            PreviewText().Visibility(xaml::Visibility::Visible);
            PreviewInfo().Visibility(xaml::Visibility::Collapsed);

            std::wstring text;
            if (stackSelected)
            {
                // Maccy PasteStackPreviewView: the queued items in paste order.
                auto const& stack = Controller().CurrentPasteStack();
                text = L"Paste stack \u2014 each paste puts the next item on the clipboard.\n\n";
                std::size_t number = 1;
                for (auto const& entry : stack->items)
                {
                    if (!entry)
                    {
                        continue;
                    }
                    std::wstring title = entry->IsImage() && entry->title.empty() ? std::wstring{ L"Image" } : entry->title;
                    text += std::to_wstring(number++) + L". " + ::Kleio::PopupText::Truncate(title, 200) + L"\n";
                }
            }
            PreviewText().Text(winrt::hstring{ text });
        }

        if (itemChanged)
        {
            PreviewScroll().ChangeView(nullptr, winrt::Windows::Foundation::IReference<double>{ 0.0 }, nullptr, true);
        }
    }

    void PopupWindow::StartAutoOpen()
    {
        // Maccy SlideoutController.startAutoOpen.
        m_previewTimer.Stop();
        if (!m_shown || m_previewOpen || m_autoOpenSuppressed || !m_lead || m_dialogOpen || m_flyoutOpen)
        {
            return;
        }
        auto& settings = Controller().Settings();
        if (!settings.OpenPreviewAutomatically())
        {
            return;
        }
        m_previewTimer.Interval(std::chrono::milliseconds(std::max(200, static_cast<int>(settings.PreviewDelay()))));
        m_previewTimer.Start();
    }

    void PopupWindow::HandleAutoOpenTick()
    {
        if (m_shown && !m_previewOpen && !m_autoOpenSuppressed && !m_dialogOpen && !m_flyoutOpen && !m_inSizeMove &&
            !m_dragInProgress && m_lead && Controller().Settings().OpenPreviewAutomatically())
        {
            TogglePreview(false);
        }
    }
}
