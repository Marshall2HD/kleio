#pragma once

// User-visible text. Strings live in Strings/<language>/Resources.resw (en-US is the source;
// the other languages derive from Maccy's Weblate translations). XAML uses x:Uid; code uses Get and
// Format with the same resource keys. See README "Translations".

#include "Core/KeyShortcut.h"
#include "Core/Localization.h"

#include <span>
#include <string>
#include <string_view>

namespace Kleio::Strings
{
    // Startup, before any window or resource is created: applies the Language setting (as the
    // app's PrimaryLanguageOverride) and lets Core look strings up. Safe to call more than once.
    void Initialize();

    // The Language setting as applied by Initialize (strings already loaded use it until restart).
    std::wstring const& InitialLanguage();
    // Sets the app's PrimaryLanguageOverride ("" = Windows display language). Takes effect for
    // resources loaded afterwards, i.e. after a restart.
    void SetLanguageOverride(std::wstring const& tag);

    // The localized string for a resource key (the key itself if it is missing, to make the gap
    // visible). Uses one cached MRT Core ResourceLoader.
    std::wstring Get(std::wstring_view key);

    // Get(key) with {0}, {1}, ... replaced by the arguments (see Localization::Format).
    template <typename... Args>
    std::wstring Format(std::wstring_view key, Args const&... args)
    {
        return Kleio::Localization::Format(Get(key), { std::wstring_view{ args }... });
    }

    // Shortcut / key names for display, localized ("Strg+Umschalt+C" in German). Never use these
    // for the serialized form in settings: that is KeyShortcut::ToString.
    std::wstring ShortcutText(KeyShortcut const& shortcut);
    std::wstring ModifiersText(Modifiers modifiers);      // "Ctrl+Shift", in Win, Ctrl, Alt, Shift order
    std::wstring ModifierName(Modifiers modifier);        // a single flag
    std::wstring KeyText(std::uint32_t virtualKey);

    // The resolved language reads right to left (Arabic, Hebrew, Persian, Central Kurdish).
    bool IsRightToLeft();
    winrt::Microsoft::UI::Xaml::FlowDirection FlowDirection();

    // Languages of the Language setting: a folder under Strings/ and its name in that language.
    struct Language
    {
        std::wstring_view tag;
        std::wstring_view name;
    };
    std::span<Language const> Languages();
}
