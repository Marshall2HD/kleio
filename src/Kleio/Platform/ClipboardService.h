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

        // WM_CLIPBOARDUPDATE does not process the clipboard right away: like Maccy, which polls the
        // pasteboard every 0.5 s, a burst of changes is treated as one copy. The check runs once the
        // clipboard has been quiet for 250 ms (at most 1 s after the first notification). This
        // matters for OLE copies (WinForms, Office, Explorer): OleSetClipboard and OleFlushClipboard
        // each change the clipboard. A flush that arrives on its own later (same OLE clipboard
        // window owning the clipboard, live data object gone) is not a new copy either.
        //
        // Checks, in order (Maccy's checkForChangesInPasteboard):
        //  0. Skip if the clipboard sequence number did not change. ExternalChange unless FromKleio.
        //  1. If Settings::IgnoreEvents: if IgnoreOnlyNextEvent, reset both to false; skip.
        //  2. Skip if any AlwaysIgnoredFormats or Settings::IgnoredFormats format is present, if no
        //     format of an enabled type (SaveText / SaveFiles / SaveImages) is present, or if
        //     "CanIncludeInClipboardHistory" is present with DWORD 0 and the copy is not from Kleio.
        //  3. Skip if the source app is ignored (IgnoredApps / IgnoreAllAppsExceptListed).
        //  4. Skip whitespace-only text without rich text; 5. skip text matching IgnoreRegexps.
        //  6. Store every persistable format (Core/ClipboardFormats.h) of an enabled type, in
        //     clipboard order: text as CF_UNICODETEXT, CF_HDROP as file paths, PNG (or CF_DIBV5 /
        //     CF_DIB converted to PNG), CF_ENHMETAFILE as metafile bits, everything else as raw bytes.
        // Source app: owner of the clipboard (GetClipboardOwner -> process), falling back to the
        // foreground window's process.

        // Puts an item on the clipboard: every stored format in its original order (Files as
        // CF_HDROP + "Preferred DropEffect" copy; Png as "PNG" and CF_DIBV5; CF_ENHMETAFILE from its
        // bits; anything else as raw bytes) plus the FromKleio marker. With removeFormatting and a
        // text content only plain text and files are written. The copy is recorded right away.
        void Copy(HistoryItem const& item, bool removeFormatting);
        // Copies a plain string (used by the item editor / "copy title").
        void CopyText(std::wstring const& text);
        // Empties the system clipboard if Settings::ClearSystemClipboard is on.
        void ClearIfEnabled();

        std::uint32_t ChangeCount() const noexcept;   // GetClipboardSequenceNumber()

    private:
        void OnClipboardUpdate();                 // WM_CLIPBOARDUPDATE: (re)schedules a check
        void ScheduleCheck(unsigned long long delayMs);
        void OnCheckTimer();
        void CheckForChanges();                   // Maccy's checkForChangesInPasteboard
        HistoryItemPtr ReadCopy(std::uint32_t sequence, int notifications, bool& externalChange);

        MessageWindow& m_window;
        Settings& m_settings;
        std::uint64_t m_handlerToken = 0;
        // GetClipboardSequenceNumber() of the last processed update (filters duplicate notifications).
        std::uint32_t m_lastSequence = 0;
        // GetTickCount64() of the first notification of the pending burst, 0 when none is pending.
        unsigned long long m_pendingSince = 0;
        int m_notifications = 0;                  // WM_CLIPBOARDUPDATEs since the last check
        int m_openRetries = 0;
        // Owner of the clipboard when the last processed copy was a live OLE data object (not yet
        // flushed); used to recognise its OleFlushClipboard.
        HWND m_liveOleOwner = nullptr;
    };
}
