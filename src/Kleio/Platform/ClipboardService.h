#pragma once

#include "Core/Event.h"
#include "Core/HistoryItem.h"
#include "Core/Settings.h"
#include "MessageWindow.h"

#include <cstdint>
#include <string>

namespace Kleio
{
    // Watches the system clipboard (AddClipboardFormatListener / WM_CLIPBOARDUPDATE) and writes
    // history items back to it. Port of Maccy's Clipboard.
    class ClipboardService
    {
    public:
        ClipboardService(MessageWindow& window, Settings& settings);
        ~ClipboardService();

        // Raised (UI thread) for every external or Kleio-made copy that passes the filters below.
        // The item has contents, application (owner/foreground exe path), timestamps = Now() and a
        // generated title; it is NOT yet in History.
        Event<HistoryItemPtr> NewCopy;
        // Raised for every clipboard change that did not come from Kleio (used to interrupt the
        // paste stack, like Maccy's interruptPasteStack).
        Event<> ExternalChange;

        // Filters applied on WM_CLIPBOARDUPDATE, in order (Maccy's checkForChangesInPasteboard):
        //  1. If Settings::IgnoreEvents: if IgnoreOnlyNextEvent, reset both to false; skip.
        //  2. Skip if any AlwaysIgnoredFormats or Settings::IgnoredFormats format is present, or if
        //     "CanIncludeInClipboardHistory" is present with DWORD 0 and the copy is not from Kleio.
        //  3. Skip if the source app is ignored (IgnoredApps / IgnoreAllAppsExceptListed).
        //  4. Read enabled types: Text (also when only CF_TEXT/CF_OEMTEXT exist), Html, Rtf
        //     (SaveText), Files from CF_HDROP (SaveFiles), Png from "PNG"/CF_DIBV5/CF_DIB (SaveImages).
        //     Whitespace-only text without rich text is skipped.
        //  5. Skip if the text matches any Settings::IgnoreRegexps.
        // Source app: owner of the clipboard (GetClipboardOwner -> process), falling back to the
        // foreground window's process.

        // Puts an item on the clipboard. Writes all contents (Files as CF_HDROP + "Preferred DropEffect"
        // copy; Png as "PNG" and CF_DIBV5; Text/Html/Rtf under their formats) plus the FromKleio marker.
        // With removeFormatting only plain text (or files/image when there is no text) is written.
        void Copy(HistoryItem const& item, bool removeFormatting);
        // Copies a plain string (used by the item editor / "copy title").
        void CopyText(std::wstring const& text);
        // Empties the system clipboard if Settings::ClearSystemClipboard is on.
        void ClearIfEnabled();

        std::uint32_t ChangeCount() const noexcept;   // GetClipboardSequenceNumber()

    private:
        void OnClipboardUpdate();

        MessageWindow& m_window;
        Settings& m_settings;
        std::uint64_t m_handlerToken = 0;
        // GetClipboardSequenceNumber() of the last processed update (filters duplicate notifications).
        std::uint32_t m_lastSequence = 0;
    };
}
