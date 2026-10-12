#include "pch.h"
#include "Platform/TrayIcon.h"

#include <cstring>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

namespace Kleio
{
    namespace
    {
        constexpr UINT CallbackMessage = WM_APP + 1;
        constexpr UINT IconId = 1;

        wchar_t GlyphFor(TrayIconStyle style) noexcept
        {
            switch (style)
            {
            case TrayIconStyle::Paperclip:
                return L''; // Attach
            case TrayIconStyle::Scissors:
                return L''; // Cut
            case TrayIconStyle::Clipboard:
            case TrayIconStyle::Kleio:
            default:
                return L''; // Paste
            }
        }

        std::wstring ExeDirectory()
        {
            std::wstring path(MAX_PATH, L'\0');
            for (;;)
            {
                const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
                if (length == 0)
                {
                    return {};
                }
                if (length < path.size())
                {
                    path.resize(length);
                    break;
                }
                path.resize(path.size() * 2);
            }
            const auto separator = path.find_last_of(L"\\/");
            return separator == std::wstring::npos ? std::wstring{} : path.substr(0, separator);
        }

        bool TaskbarUsesLightTheme() noexcept
        {
            DWORD value = 0;
            DWORD size = sizeof(value);
            const LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
            return status == ERROR_SUCCESS && value != 0;
        }

        UINT CurrentDpi(HWND hwnd) noexcept
        {
            UINT dpi = hwnd ? GetDpiForWindow(hwnd) : 0;
            if (dpi == 0)
            {
                dpi = GetDpiForSystem();
            }
            return dpi == 0 ? 96 : dpi;
        }

        // Creates an icon from top-down, straight-alpha BGRA pixels.
        HICON IconFromBgra(int size, std::vector<std::uint8_t> const& pixels) noexcept
        {
            BITMAPV5HEADER header{};
            header.bV5Size = sizeof(header);
            header.bV5Width = size;
            header.bV5Height = -size; // top-down
            header.bV5Planes = 1;
            header.bV5BitCount = 32;
            header.bV5Compression = BI_BITFIELDS;
            header.bV5RedMask = 0x00FF0000;
            header.bV5GreenMask = 0x0000FF00;
            header.bV5BlueMask = 0x000000FF;
            header.bV5AlphaMask = 0xFF000000;

            void* bits = nullptr;
            HDC screen = GetDC(nullptr);
            wil::unique_hbitmap color{ CreateDIBSection(screen, reinterpret_cast<BITMAPINFO const*>(&header), DIB_RGB_COLORS, &bits, nullptr, 0) };
            ReleaseDC(nullptr, screen);
            if (!color || !bits)
            {
                return nullptr;
            }
            std::memcpy(bits, pixels.data(), (std::min)(pixels.size(), static_cast<std::size_t>(size) * size * 4));
            GdiFlush();

            // Monochrome AND mask, all zero: the alpha channel decides transparency.
            std::vector<std::uint8_t> maskBits(static_cast<std::size_t>((size + 15) / 16) * 2 * size, 0);
            wil::unique_hbitmap mask{ CreateBitmap(size, size, 1, 1, maskBits.data()) };
            if (!mask)
            {
                return nullptr;
            }

            ICONINFO info{};
            info.fIcon = TRUE;
            info.hbmMask = mask.get();
            info.hbmColor = color.get();
            return CreateIconIndirect(&info);
        }

        // Renders a Segoe Fluent Icons glyph in `color` with antialiased alpha.
        HICON RenderGlyph(wchar_t glyph, int size, COLORREF color, double opacity) noexcept
        {
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = size;
            info.bmiHeader.biHeight = -size;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;

            void* bits = nullptr;
            wil::unique_hbitmap bitmap{ CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0) };
            if (!bitmap || !bits)
            {
                return nullptr;
            }
            const std::size_t byteCount = static_cast<std::size_t>(size) * size * 4;
            std::memset(bits, 0, byteCount);

            wil::unique_hdc dc{ CreateCompatibleDC(nullptr) };
            if (!dc)
            {
                return nullptr;
            }
            const HGDIOBJ oldBitmap = SelectObject(dc.get(), bitmap.get());

            auto createFont = [size](wchar_t const* face) {
                return CreateFontW(-size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                    CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
            };
            wil::unique_hfont font{ createFont(L"Segoe Fluent Icons") };
            HGDIOBJ oldFont = font ? SelectObject(dc.get(), font.get()) : nullptr;
            wchar_t face[LF_FACESIZE]{};
            GetTextFaceW(dc.get(), LF_FACESIZE, face);
            if (_wcsicmp(face, L"Segoe Fluent Icons") != 0)
            {
                // Windows 10 fallback.
                if (oldFont)
                {
                    SelectObject(dc.get(), oldFont);
                }
                font.reset(createFont(L"Segoe MDL2 Assets"));
                oldFont = font ? SelectObject(dc.get(), font.get()) : nullptr;
            }

            SetBkMode(dc.get(), TRANSPARENT);
            SetTextColor(dc.get(), RGB(255, 255, 255));
            RECT rect{ 0, 0, size, size };
            wchar_t text[2]{ glyph, L'\0' };
            DrawTextW(dc.get(), text, 1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_NOCLIP);
            GdiFlush();

            if (oldFont)
            {
                SelectObject(dc.get(), oldFont);
            }
            SelectObject(dc.get(), oldBitmap);

            // White-on-black grayscale coverage -> alpha.
            std::vector<std::uint8_t> pixels(byteCount);
            auto const* source = static_cast<std::uint8_t const*>(bits);
            for (std::size_t i = 0; i < byteCount; i += 4)
            {
                const std::uint8_t coverage = (std::max)({ source[i], source[i + 1], source[i + 2] });
                pixels[i + 0] = GetBValue(color);
                pixels[i + 1] = GetGValue(color);
                pixels[i + 2] = GetRValue(color);
                pixels[i + 3] = static_cast<std::uint8_t>(coverage * opacity + 0.5);
            }
            return IconFromBgra(size, pixels);
        }

        // Returns a copy of `icon` with its opacity multiplied by `opacity`.
        HICON FadeIcon(HICON icon, int size, double opacity) noexcept
        {
            ICONINFO info{};
            if (!GetIconInfo(icon, &info))
            {
                return nullptr;
            }
            wil::unique_hbitmap color{ info.hbmColor };
            wil::unique_hbitmap mask{ info.hbmMask };
            if (!color)
            {
                return nullptr;
            }

            BITMAPINFO bitmapInfo{};
            bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bitmapInfo.bmiHeader.biWidth = size;
            bitmapInfo.bmiHeader.biHeight = -size;
            bitmapInfo.bmiHeader.biPlanes = 1;
            bitmapInfo.bmiHeader.biBitCount = 32;
            bitmapInfo.bmiHeader.biCompression = BI_RGB;

            const std::size_t byteCount = static_cast<std::size_t>(size) * size * 4;
            std::vector<std::uint8_t> pixels(byteCount);
            std::vector<std::uint8_t> maskPixels(byteCount);
            wil::unique_hdc dc{ CreateCompatibleDC(nullptr) };
            if (!dc || !GetDIBits(dc.get(), color.get(), 0, size, pixels.data(), &bitmapInfo, DIB_RGB_COLORS))
            {
                return nullptr;
            }
            bool hasAlpha = false;
            for (std::size_t i = 3; i < byteCount; i += 4)
            {
                if (pixels[i] != 0)
                {
                    hasAlpha = true;
                    break;
                }
            }
            if (!hasAlpha && mask && GetDIBits(dc.get(), mask.get(), 0, size, maskPixels.data(), &bitmapInfo, DIB_RGB_COLORS))
            {
                for (std::size_t i = 0; i < byteCount; i += 4)
                {
                    pixels[i + 3] = maskPixels[i] ? 0 : 255; // AND mask: white = transparent
                }
            }
            for (std::size_t i = 3; i < byteCount; i += 4)
            {
                pixels[i] = static_cast<std::uint8_t>(pixels[i] * opacity + 0.5);
            }
            return IconFromBgra(size, pixels);
        }

        void EnableDarkContextMenus() noexcept
        {
            static const bool once = [] {
                // Undocumented but stable since Windows 10 1903: uxtheme ordinal 135 is
                // SetPreferredAppMode, 136 FlushMenuThemes. Best effort.
                const HMODULE uxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
                if (uxtheme)
                {
                    using SetPreferredAppModeFn = int(WINAPI*)(int);
                    using FlushMenuThemesFn = void(WINAPI*)();
                    if (auto setMode = reinterpret_cast<SetPreferredAppModeFn>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(135))))
                    {
                        setMode(1); // AllowDark
                    }
                    if (auto flush = reinterpret_cast<FlushMenuThemesFn>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(136))))
                    {
                        flush();
                    }
                }
                return true;
            }();
            (void)once;
        }
    }

    TrayIcon::TrayIcon(MessageWindow& window) : m_window(window)
    {
        m_taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");
        // Let the (non-elevated) Explorer broadcast reach us even if we run elevated.
        if (m_taskbarCreatedMessage != 0 && m_window.Handle())
        {
            ChangeWindowMessageFilterEx(m_window.Handle(), m_taskbarCreatedMessage, MSGFLT_ALLOW, nullptr);
        }
        try
        {
            EnableDarkContextMenus();
        }
        catch (...)
        {
        }
        m_handlerToken = m_window.AddHandler([this](UINT message, WPARAM wParam, LPARAM lParam) {
            return OnMessage(message, wParam, lParam);
        });
    }

    TrayIcon::~TrayIcon()
    {
        if (m_visible)
        {
            Notify(NIM_DELETE);
            m_visible = false;
        }
        m_window.RemoveHandler(m_handlerToken);
        if (m_icon)
        {
            DestroyIcon(m_icon);
            m_icon = nullptr;
        }
    }

    void TrayIcon::Show(TrayIconStyle style)
    {
        m_style = style;
        RenderIcon();
        if (m_visible)
        {
            Notify(NIM_MODIFY);
        }
        else
        {
            Add();
        }
    }

    void TrayIcon::Hide()
    {
        if (m_visible)
        {
            Notify(NIM_DELETE);
            m_visible = false;
        }
    }

    void TrayIcon::SetStyle(TrayIconStyle style)
    {
        if (m_style == style && m_icon)
        {
            return;
        }
        m_style = style;
        if (m_visible)
        {
            RenderIcon();
            Notify(NIM_MODIFY);
        }
    }

    void TrayIcon::SetDisabled(bool disabled)
    {
        if (m_disabled == disabled)
        {
            return;
        }
        m_disabled = disabled;
        if (m_visible)
        {
            RenderIcon();
            Notify(NIM_MODIFY);
        }
    }

    void TrayIcon::SetTooltip(std::wstring const& text)
    {
        m_tooltip = text.substr(0, 127);
        if (m_visible)
        {
            Notify(NIM_MODIFY);
        }
    }

    std::optional<RECT> TrayIcon::Rect() const
    {
        if (!m_visible)
        {
            return std::nullopt;
        }
        NOTIFYICONIDENTIFIER identifier{};
        identifier.cbSize = sizeof(identifier);
        identifier.hWnd = m_window.Handle();
        identifier.uID = IconId;
        RECT rect{};
        if (FAILED(Shell_NotifyIconGetRect(&identifier, &rect)))
        {
            return std::nullopt;
        }
        return rect;
    }

    void TrayIcon::Add()
    {
        m_visible = true; // desired state; re-added on TaskbarCreated if Explorer isn't ready yet
        if (!m_icon)
        {
            RenderIcon();
        }
        if (!Notify(NIM_ADD))
        {
            // A stale icon from a previous instance may still be registered.
            Notify(NIM_DELETE);
            if (!Notify(NIM_ADD))
            {
                OutputDebugStringW(L"Kleio: Shell_NotifyIcon(NIM_ADD) failed\n");
                return;
            }
        }
        Notify(NIM_SETVERSION);
    }

    bool TrayIcon::Notify(DWORD message)
    {
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = m_window.Handle();
        data.uID = IconId;
        if (!data.hWnd)
        {
            return false;
        }
        if (message == NIM_SETVERSION)
        {
            data.uVersion = NOTIFYICON_VERSION_4;
        }
        else if (message != NIM_DELETE)
        {
            data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
            data.uCallbackMessage = CallbackMessage;
            data.hIcon = m_icon;
            const std::wstring tip = m_tooltip.empty() ? std::wstring{ L"Kleio" } : m_tooltip;
            wcsncpy_s(data.szTip, std::size(data.szTip), tip.c_str(), _TRUNCATE);
        }
        return Shell_NotifyIconW(message, &data) != FALSE;
    }

    void TrayIcon::RenderIcon()
    {
        const HWND hwnd = m_window.Handle();
        const UINT dpi = CurrentDpi(hwnd);
        int size = GetSystemMetricsForDpi(SM_CXSMICON, dpi);
        if (size <= 0)
        {
            size = MulDiv(16, static_cast<int>(dpi), 96);
        }
        const double opacity = m_disabled ? 0.5 : 1.0;

        HICON icon = nullptr;
        if (m_style == TrayIconStyle::Kleio)
        {
            const std::wstring path = ExeDirectory() + L"\\Assets\\Kleio.ico";
            // LoadImageW picks the best matching image from the .ico and scales it to `size`.
            HICON loaded = static_cast<HICON>(LoadImageW(nullptr, path.c_str(), IMAGE_ICON, size, size, LR_LOADFROMFILE));
            if (loaded)
            {
                if (m_disabled)
                {
                    icon = FadeIcon(loaded, size, opacity);
                    DestroyIcon(loaded);
                }
                else
                {
                    icon = loaded;
                }
            }
        }
        if (!icon)
        {
            const COLORREF color = TaskbarUsesLightTheme() ? RGB(0, 0, 0) : RGB(255, 255, 255);
            icon = RenderGlyph(GlyphFor(m_style), size, color, opacity);
        }
        if (!icon)
        {
            return; // keep the previous icon
        }
        if (m_icon)
        {
            DestroyIcon(m_icon);
        }
        m_icon = icon;
    }

    std::optional<LRESULT> TrayIcon::OnMessage(UINT message, WPARAM wParam, LPARAM lParam)
    {
        try
        {
            if (message == CallbackMessage)
            {
                // NOTIFYICON_VERSION_4: LOWORD(lParam) = event, wParam = anchor coordinates.
                const UINT event = LOWORD(lParam);
                switch (event)
                {
                case NIN_SELECT:
                case NIN_KEYSELECT:
                {
                    // NIN_KEYSELECT can arrive twice for one Enter press; ignore immediate repeats.
                    const ULONGLONG now = GetTickCount64();
                    if (now - m_lastInvokeTick < 150)
                    {
                        break;
                    }
                    m_lastInvokeTick = now;
                    Invoked(CurrentModifiers());
                    break;
                }
                case WM_CONTEXTMENU:
                    ShowContextMenu(static_cast<short>(LOWORD(wParam)), static_cast<short>(HIWORD(wParam)));
                    break;
                default:
                    break;
                }
                return LRESULT{ 0 };
            }

            if (m_taskbarCreatedMessage != 0 && message == m_taskbarCreatedMessage)
            {
                // Explorer restarted: our icon is gone.
                if (m_visible)
                {
                    RenderIcon();
                    Add();
                }
                return std::nullopt;
            }

            switch (message)
            {
            case WM_SETTINGCHANGE:
                if (lParam && CompareStringOrdinal(reinterpret_cast<wchar_t const*>(lParam), -1, L"ImmersiveColorSet", -1, TRUE) == CSTR_EQUAL)
                {
                    if (m_visible)
                    {
                        RenderIcon();
                        Notify(NIM_MODIFY);
                    }
                }
                break;
            case WM_DPICHANGED:
            case WM_DISPLAYCHANGE:
                if (m_visible)
                {
                    RenderIcon();
                    Notify(NIM_MODIFY);
                }
                break;
            default:
                break;
            }
        }
        catch (...)
        {
            OutputDebugStringW(L"Kleio: exception in the tray icon handler\n");
        }
        return std::nullopt;
    }

    void TrayIcon::ShowContextMenu(int x, int y)
    {
        if (!MenuProvider)
        {
            return;
        }
        std::vector<MenuItem> items = MenuProvider();
        if (items.empty())
        {
            return;
        }

        wil::unique_hmenu menu{ CreatePopupMenu() };
        if (!menu)
        {
            return;
        }
        for (std::size_t i = 0; i < items.size(); ++i)
        {
            auto const& item = items[i];
            if (item.text.empty())
            {
                AppendMenuW(menu.get(), MF_SEPARATOR, 0, nullptr);
                continue;
            }
            UINT flags = MF_STRING;
            if (item.checked)
            {
                flags |= MF_CHECKED;
            }
            if (!item.enabled)
            {
                flags |= MF_GRAYED;
            }
            AppendMenuW(menu.get(), flags, static_cast<UINT_PTR>(i + 1), item.text.c_str());
        }

        const HWND hwnd = m_window.Handle();
        // Required so the menu closes when the user clicks elsewhere.
        SetForegroundWindow(hwnd);
        UINT flags = TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY | TPM_BOTTOMALIGN;
        flags |= GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN;
        const UINT command = static_cast<UINT>(TrackPopupMenuEx(menu.get(), flags, x, y, hwnd, nullptr));
        PostMessageW(hwnd, WM_NULL, 0, 0);

        if (command >= 1 && command <= items.size())
        {
            auto action = items[command - 1].action;
            if (action)
            {
                try
                {
                    action();
                }
                catch (...)
                {
                    OutputDebugStringW(L"Kleio: exception in a tray menu action\n");
                }
            }
        }
    }
}
