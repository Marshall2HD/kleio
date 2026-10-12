#include "pch.h"
#include "Strings.h"

#include "Core/Settings.h"

namespace Kleio::Strings
{
    namespace
    {
        using winrt::Microsoft::Windows::ApplicationModel::Resources::ResourceLoader;

        constexpr Language kLanguages[] = {
            { L"ar", L"\u0627\u0644\u0639\u0631\u0628\u064A\u0629" },
            { L"be", L"\u0411\u0435\u043B\u0430\u0440\u0443\u0441\u043A\u0430\u044F" },
            { L"bn", L"\u09AC\u09BE\u0982\u09B2\u09BE" },
            { L"bs", L"Bosanski" },
            { L"ca", L"Catal\u00E0" },
            { L"cs", L"\u010Ce\u0161tina" },
            { L"de", L"Deutsch" },
            { L"el", L"\u0395\u03BB\u03BB\u03B7\u03BD\u03B9\u03BA\u03AC" },
            { L"en-US", L"English" },
            { L"eo", L"Esperanto" },
            { L"es", L"Espa\u00F1ol" },
            { L"fa", L"\u0641\u0627\u0631\u0633\u06CC" },
            { L"fi", L"Suomi" },
            { L"fr", L"Fran\u00E7ais" },
            { L"he", L"\u05E2\u05D1\u05E8\u05D9\u05EA" },
            { L"hi", L"\u0939\u093F\u0928\u094D\u0926\u0940" },
            { L"hr", L"Hrvatski" },
            { L"hu", L"Magyar" },
            { L"id", L"Bahasa Indonesia" },
            { L"it", L"Italiano" },
            { L"ja", L"\u65E5\u672C\u8A9E" },
            { L"km", L"\u1781\u17D2\u1798\u17C2\u179A" },
            { L"ko", L"\uD55C\uAD6D\uC5B4" },
            { L"ku-Arab", L"\u06A9\u0648\u0631\u062F\u06CC\u06CC \u0646\u0627\u0648\u06D5\u0646\u062F\u06CC" },
            { L"lt", L"Lietuvi\u0173" },
            { L"lv", L"Latvie\u0161u" },
            { L"nb-NO", L"Norsk bokm\u00E5l" },
            { L"nl", L"Nederlands" },
            { L"pl", L"Polski" },
            { L"pt-BR", L"Portugu\u00EAs (Brasil)" },
            { L"pt-PT", L"Portugu\u00EAs (Portugal)" },
            { L"ro", L"Rom\u00E2n\u0103" },
            { L"ru", L"\u0420\u0443\u0441\u0441\u043A\u0438\u0439" },
            { L"sl", L"Sloven\u0161\u010Dina" },
            { L"sv", L"Svenska" },
            { L"ta", L"\u0BA4\u0BAE\u0BBF\u0BB4\u0BCD" },
            { L"th", L"\u0E44\u0E17\u0E22" },
            { L"tr", L"T\u00FCrk\u00E7e" },
            { L"uk", L"\u0423\u043A\u0440\u0430\u0457\u043D\u0441\u044C\u043A\u0430" },
            { L"uz-Latn", L"O\u02BBzbekcha" },
            { L"vi", L"Ti\u1EBFng Vi\u1EC7t" },
            { L"zh-Hans", L"\u7B80\u4F53\u4E2D\u6587" },
            { L"zh-Hant", L"\u7E41\u9AD4\u4E2D\u6587" },
        };

        ResourceLoader const& Loader()
        {
            static ResourceLoader const loader = []() -> ResourceLoader {
                try
                {
                    return ResourceLoader{};
                }
                catch (...)
                {
                    OutputDebugStringW(L"Kleio: resources.pri not available\n");
                    return nullptr;
                }
            }();
            return loader;
        }

        std::wstring& InitialLanguageStorage()
        {
            static std::wstring language;
            return language;
        }

        // Core's lookup: empty when missing, so Core falls back to its English text.
        std::wstring Lookup(std::wstring_view key)
        {
            if (auto const& loader = Loader())
            {
                return std::wstring{ loader.GetString(winrt::hstring{ key }) };
            }
            return {};
        }

        // Resource key of a key name that is printed differently on keyboards in other languages.
        wchar_t const* KeyResource(std::uint32_t virtualKey) noexcept
        {
            switch (virtualKey)
            {
            case VK_SPACE: return L"Key_Space";
            case VK_RETURN: return L"Key_Enter";
            case VK_DELETE: return L"Key_Delete";
            case VK_BACK: return L"Key_Backspace";
            case VK_TAB: return L"Key_Tab";
            case VK_ESCAPE: return L"Key_Escape";
            case VK_INSERT: return L"Key_Insert";
            case VK_HOME: return L"Key_Home";
            case VK_END: return L"Key_End";
            case VK_PRIOR: return L"Key_PageUp";
            case VK_NEXT: return L"Key_PageDown";
            case VK_UP: return L"Key_Up";
            case VK_DOWN: return L"Key_Down";
            case VK_LEFT: return L"Key_Left";
            case VK_RIGHT: return L"Key_Right";
            default: return nullptr;
            }
        }
    }

    void Initialize()
    {
        Localization::SetLookup(&Lookup);
        try
        {
            InitialLanguageStorage() = Settings::Instance().Language();
        }
        catch (...)
        {
        }
        // Windows also stores the override per package; setting it on every start keeps it in line
        // with the setting.
        SetLanguageOverride(InitialLanguage());
    }

    std::wstring const& InitialLanguage()
    {
        return InitialLanguageStorage();
    }

    void SetLanguageOverride(std::wstring const& tag)
    {
        bool known = tag.empty();
        for (auto const& entry : kLanguages)
        {
            known = known || entry.tag == tag;
        }
        try
        {
            // "" removes the override: the Windows language preferences apply.
            winrt::Microsoft::Windows::Globalization::ApplicationLanguages::PrimaryLanguageOverride(
                winrt::hstring{ known ? tag : std::wstring{} });
        }
        catch (...)
        {
            OutputDebugStringW(L"Kleio: could not apply the language override\n");
        }
    }

    std::wstring Get(std::wstring_view key)
    {
        try
        {
            if (auto text = Lookup(key); !text.empty())
            {
                return text;
            }
        }
        catch (...)
        {
        }
        return std::wstring{ key };
    }

    std::wstring ModifierName(Modifiers modifier)
    {
        switch (modifier)
        {
        case Modifiers::Ctrl: return Get(L"Key_Ctrl");
        case Modifiers::Alt: return Get(L"Key_Alt");
        case Modifiers::Shift: return Get(L"Key_Shift");
        case Modifiers::Win: return Get(L"Key_Win");
        default: return {};
        }
    }

    std::wstring ModifiersText(Modifiers modifiers)
    {
        std::wstring text;
        // Same order as KeyShortcut::ToString (the Windows convention).
        for (auto const flag : { Modifiers::Win, Modifiers::Ctrl, Modifiers::Alt, Modifiers::Shift })
        {
            if (HasFlag(modifiers, flag))
            {
                if (!text.empty())
                {
                    text += L'+';
                }
                text += ModifierName(flag);
            }
        }
        return text;
    }

    std::wstring KeyText(std::uint32_t virtualKey)
    {
        if (auto const resource = KeyResource(virtualKey))
        {
            return Get(resource);
        }
        return KeyShortcut::KeyName(virtualKey);
    }

    std::wstring ShortcutText(KeyShortcut const& shortcut)
    {
        if (shortcut.IsEmpty())
        {
            return {};
        }
        std::wstring text = ModifiersText(shortcut.modifiers);
        if (!text.empty())
        {
            text += L'+';
        }
        return text + KeyText(shortcut.key);
    }

    bool IsRightToLeft()
    {
        static bool const rightToLeft = Get(L"FlowDirection") == L"RightToLeft";
        return rightToLeft;
    }

    winrt::Microsoft::UI::Xaml::FlowDirection FlowDirection()
    {
        return IsRightToLeft() ? winrt::Microsoft::UI::Xaml::FlowDirection::RightToLeft
                               : winrt::Microsoft::UI::Xaml::FlowDirection::LeftToRight;
    }

    std::span<Language const> Languages()
    {
        return kLanguages;
    }
}
