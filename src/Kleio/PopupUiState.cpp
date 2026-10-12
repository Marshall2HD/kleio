#include "pch.h"
#include "PopupUiState.h"

namespace Kleio
{
    std::wstring PopupUiState::ShortcutHint(std::wstring const& key, Modifiers pressedModifiers) const
    {
        if (key.empty())
        {
            return {};
        }

        std::uint32_t virtualKey = static_cast<std::uint32_t>(key[0]);
        if (virtualKey >= L'a' && virtualKey <= L'z')
        {
            virtualKey = virtualKey - L'a' + L'A';
        }

        // Maccy's KeyShortcut.create: one shortcut per item action (copy / paste / paste plain).
        std::vector<Modifiers> all;
        for (auto action : { ItemAction::Copy, ItemAction::Paste, ItemAction::PasteWithoutFormatting })
        {
            const Modifiers modifiers = ModifiersFor(action);
            if (modifiers != Modifiers::None && std::find(all.begin(), all.end(), modifiers) == all.end())
            {
                all.push_back(modifiers);
            }
        }
        if (all.empty())
        {
            return {};
        }

        // The "primary" shortcut (Maccy: the Command one) is shown unless the held modifiers match
        // another one exactly.
        std::size_t chosen = 0;
        for (std::size_t i = 0; i < all.size(); ++i)
        {
            if (all[i] == Modifiers::Ctrl)
            {
                chosen = i;
                break;
            }
        }
        const Modifiers pressed = pressedModifiers & (Modifiers::Ctrl | Modifiers::Alt | Modifiers::Shift);
        if (pressed != Modifiers::None)
        {
            for (std::size_t i = 0; i < all.size(); ++i)
            {
                if (all[i] == pressed)
                {
                    chosen = i;
                    break;
                }
            }
        }

        KeyShortcut shortcut;
        shortcut.modifiers = all[chosen];
        shortcut.key = virtualKey;
        return shortcut.ToString();
    }

    namespace PopupText
    {
        std::wstring FormatTimestamp(Timestamp milliseconds)
        {
            if (milliseconds <= 0)
            {
                return L"\u2014";
            }

            ULARGE_INTEGER value{};
            value.QuadPart = static_cast<ULONGLONG>(milliseconds) * 10000ULL + 116444736000000000ULL;
            FILETIME fileTime{};
            fileTime.dwLowDateTime = value.LowPart;
            fileTime.dwHighDateTime = value.HighPart;

            SYSTEMTIME utc{};
            SYSTEMTIME local{};
            if (!FileTimeToSystemTime(&fileTime, &utc) || !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local))
            {
                return L"\u2014";
            }

            wchar_t date[128]{};
            wchar_t time[128]{};
            GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &local, nullptr, date, 128, nullptr);
            GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &local, nullptr, time, 128);

            std::wstring result{ date };
            if (time[0] != L'\0')
            {
                if (!result.empty())
                {
                    result += L' ';
                }
                result += time;
            }
            return result;
        }

        std::wstring Truncate(std::wstring text, std::size_t maxLength)
        {
            if (text.size() > maxLength)
            {
                // Don't split a surrogate pair.
                std::size_t cut = maxLength;
                if (cut > 0 && IS_HIGH_SURROGATE(text[cut - 1]))
                {
                    --cut;
                }
                text.resize(cut);
                text += L'\u2026';
            }
            return text;
        }
    }
}
