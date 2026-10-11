#pragma once

#include "KeyShortcut.h"

namespace Kleio
{
    // Port of Maccy's HistoryItemAction with macOS modifiers mapped to Windows ones:
    //   Command -> Ctrl, Option -> Alt.
    // Default (pasteByDefault = false, removeFormattingByDefault = false):
    //   Enter / click / Ctrl+N          copy
    //   Alt+Enter / Alt+click / Alt+N   paste
    //   Alt+Shift+Enter/click/N         paste without formatting
    enum class ItemAction { Unknown, Copy, Paste, PasteWithoutFormatting };

    // Action for a key/click with the given modifiers (Maccy's HistoryItemAction(modifierFlags)).
    // Modifiers::None returns Unknown: plain Enter/click is the *default* action, handled by
    // AppController::Select (copy, removing formatting if removeFormattingByDefault, then paste if
    // pasteByDefault).
    ItemAction ActionForModifiers(Modifiers modifiers, bool pasteByDefault, bool removeFormattingByDefault) noexcept;
    // Inverse: the modifiers that trigger `action` (used for the "Ctrl+1" style hints and the
    // General settings page description). Returns Modifiers::None for Unknown.
    Modifiers ModifiersForAction(ItemAction action, bool pasteByDefault, bool removeFormattingByDefault) noexcept;
}
