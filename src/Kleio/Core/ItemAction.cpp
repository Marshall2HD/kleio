#include "pch.h"
#include "Core/ItemAction.h"

namespace Kleio
{
    // Direct port of Maccy's HistoryItemAction (Command -> Ctrl, Option -> Alt). Modifier sets are
    // compared for equality, exactly like Swift's `switch modifierFlags { case .command ... }`.
    ItemAction ActionForModifiers(Modifiers modifiers, bool pasteByDefault, bool removeFormattingByDefault) noexcept
    {
        const Modifiers ctrl = Modifiers::Ctrl;
        const Modifiers alt = Modifiers::Alt;
        const Modifiers altShift = Modifiers::Alt | Modifiers::Shift;
        const Modifiers ctrlShift = Modifiers::Ctrl | Modifiers::Shift;

        if (modifiers == ctrl)
        {
            if (!pasteByDefault)
            {
                return ItemAction::Copy;
            }
            return removeFormattingByDefault ? ItemAction::PasteWithoutFormatting : ItemAction::Paste;
        }
        if (modifiers == alt)
        {
            if (pasteByDefault)
            {
                return ItemAction::Copy;
            }
            return removeFormattingByDefault ? ItemAction::PasteWithoutFormatting : ItemAction::Paste;
        }
        if (modifiers == altShift)
        {
            if (pasteByDefault)
            {
                return ItemAction::Unknown;
            }
            return removeFormattingByDefault ? ItemAction::Paste : ItemAction::PasteWithoutFormatting;
        }
        if (modifiers == ctrlShift)
        {
            if (!pasteByDefault)
            {
                return ItemAction::Unknown;
            }
            return removeFormattingByDefault ? ItemAction::Paste : ItemAction::PasteWithoutFormatting;
        }
        return ItemAction::Unknown;
    }

    Modifiers ModifiersForAction(ItemAction action, bool pasteByDefault, bool removeFormattingByDefault) noexcept
    {
        const bool p = pasteByDefault;
        const bool r = removeFormattingByDefault;
        switch (action)
        {
        case ItemAction::Copy:
            // .copy where !paste -> command; .copy where paste -> option
            return p ? Modifiers::Alt : Modifiers::Ctrl;
        case ItemAction::Paste:
            if (p && !r)
            {
                return Modifiers::Ctrl;
            }
            if (!p && !r)
            {
                return Modifiers::Alt;
            }
            if (!p && r)
            {
                return Modifiers::Alt | Modifiers::Shift;
            }
            return Modifiers::Ctrl | Modifiers::Shift;   // p && r
        case ItemAction::PasteWithoutFormatting:
            if (p && r)
            {
                return Modifiers::Ctrl;
            }
            if (!p && r)
            {
                return Modifiers::Alt;
            }
            if (!p && !r)
            {
                return Modifiers::Alt | Modifiers::Shift;
            }
            return Modifiers::Ctrl | Modifiers::Shift;   // p && !r
        case ItemAction::Unknown:
        default:
            return Modifiers::None;
        }
    }
}
