#pragma once

// Shared, popup-wide UI state read by the history rows (HistoryItemRow) while they render, plus small
// formatting helpers. Plain C++; UI thread only. Owned by the popup (PopupWindow).

#include "Core/HistoryItem.h"
#include "Core/ItemAction.h"
#include "Core/KeyShortcut.h"
#include "Core/Settings.h"

#include <functional>
#include <string>

namespace winrt::Kleio::implementation
{
    struct HistoryItemRow;
}

namespace Kleio
{
    struct PopupUiState
    {
        // Intentionally leaked: it may hold WinRT references that must not be released during static
        // destruction (after the XAML framework has shut down).
        static PopupUiState& Get()
        {
            static PopupUiState* instance = new PopupUiState();
            return *instance;
        }

        // Modifier keys currently held (Ctrl/Alt/Shift only). Drives the shortcut hints
        // (Maccy's KeyShortcut.isVisible).
        Modifiers modifiers = Modifiers::None;
        // Multi-selection in progress: rows show their selection order badge.
        bool multiSelect = false;
        // Number of selected items (rows' accessibility label "Selected, i of n").
        std::size_t selectionCount = 0;

        // Settings snapshot (refreshed by the popup when settings change).
        bool showApplicationIcons = false;
        bool showHexColorSwatch = true;
        double imageMaxHeight = 40;
        HighlightMatch highlightMatch = HighlightMatch::Bold;
        bool pasteByDefault = false;
        bool removeFormattingByDefault = false;

        // Rasterization scale of the monitor the popup is on (DPI / 96).
        double scale = 1.0;
        // Widest image thumbnail that fits into a row, in DIPs.
        double thumbnailMaxWidth = 300;

        // Row -> popup notifications (hover selection and context menu).
        std::function<void(winrt::Kleio::implementation::HistoryItemRow&, winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&)> rowPointerMoved;
        std::function<void(winrt::Kleio::implementation::HistoryItemRow&, winrt::Microsoft::UI::Xaml::Input::RightTappedRoutedEventArgs const&)> rowRightTapped;

        // "Ctrl+1" style hint for an item whose shortcut key is `key` ("1".."9" or a pin letter),
        // following the held modifiers like Maccy's KeyShortcut.isVisible. Empty when there is none.
        std::wstring ShortcutHint(std::wstring const& key) const { return ShortcutHint(key, modifiers); }
        // Same for explicitly given held modifiers (None: the primary shortcut, e.g. for screen readers).
        std::wstring ShortcutHint(std::wstring const& key, Modifiers pressedModifiers) const;

        // Modifiers that trigger `action` with the current settings (None if no modifiers do).
        Modifiers ModifiersFor(ItemAction action) const noexcept
        {
            return ModifiersForAction(action, pasteByDefault, removeFormattingByDefault);
        }

    private:
        PopupUiState() = default;
    };

    namespace PopupText
    {
        // Short date + time in the user's locale, e.g. "11/10/2026 14:05".
        std::wstring FormatTimestamp(Timestamp milliseconds);
        // Truncates to `maxLength` characters, appending an ellipsis when something was cut.
        std::wstring Truncate(std::wstring text, std::size_t maxLength);
    }
}
