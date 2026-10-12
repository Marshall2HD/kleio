#pragma once

#include "Event.h"
#include "HistoryItem.h"
#include "Search.h"
#include "Settings.h"
#include "Storage.h"

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace Kleio
{
    // In-memory, sorted view of the clipboard history backed by Storage. Port of Maccy's History +
    // PinManager business logic (UI concerns such as selection live in the popup).
    // UI thread only.
    class History
    {
    public:
        History(Storage& storage, Settings& settings);
        ~History();   // unsubscribes from Settings::Changed
        History(History const&) = delete;
        History& operator=(History const&) = delete;

        // Loads everything from storage, sorts, trims to HistorySize. Raises Changed.
        void Load();

        // Adds a freshly copied item (Maccy's History.add):
        //  * If an existing item supersedes `item` (existing->Supersedes(*item), i.e. `item` is a
        //    duplicate), the new item takes over its contents, firstCopiedAt, numberOfCopies
        //    (+= existing), pin, title and application (Maccy keeps the original source app; only
        //    when the taken-over contents carry the FromKleio marker the new application is kept);
        //    the old row is deleted.
        //  * Otherwise, if `item` carries the ContentType::Modified marker naming the change count of
        //    an item added in this session (Maccy's isModified / sessionLog), that item is the one
        //    replaced: same as above, except that `item` keeps its own (modified) contents.
        //  * Persists, inserts at the sorted position, trims unpinned items beyond HistorySize, and
        //    records the item under item->changeCount in the session log (when non-zero).
        // Returns the stored item. Raises ItemAdded(item, wasDuplicate) then Changed.
        HistoryItemPtr Add(HistoryItemPtr item);

        void Delete(HistoryItemPtr const& item);           // raises Changed
        void Clear();                                      // removes all unpinned; raises Changed
        void ClearAll();                                   // removes everything; raises Changed

        // Pins with a random available letter (see AvailablePins) or unpins. Raises Changed.
        // No-op when no pin letter is available.
        void TogglePin(HistoryItemPtr const& item);
        // Assigns a specific pin letter (must be available or already this item's). Raises Changed.
        void UpdatePin(HistoryItemPtr const& item, std::wstring const& pin);
        // Moves a pinned item within the pinned list (indices into PinnedItems()); persists PinOrder.
        // `to` is the item's index *after* the move (clamped to the last index), e.g. MovePin(0, 1)
        // swaps the first two pins.
        void MovePin(std::size_t from, std::size_t to);

        void UpdateTitle(HistoryItemPtr const& item, std::wstring title);   // e.g. after OCR; raises Changed
        // Replaces an item's text content (item editor): sets Text, drops every other content
        // (Maccy's ItemEditorView), regenerates title.
        void UpdateText(HistoryItemPtr const& item, std::wstring const& text);
        void RegenerateTitles();                           // after ShowSpecialSymbols changes

        // All items, already sorted. Pinned items in pin order.
        std::vector<HistoryItemPtr> const& PinnedItems() const noexcept { return m_pinned; }
        std::vector<HistoryItemPtr> const& UnpinnedItems() const noexcept { return m_unpinned; }
        // Pinned + unpinned concatenated according to PinTo.
        std::vector<HistoryItemPtr> AllItems() const;
        HistoryItemPtr FindById(std::int64_t id) const;
        HistoryItemPtr MostRecent() const;                 // most recently copied item (any)

        // Letters usable as pins: b..y minus a, c, q, v, w, x, y, z (reserved for select all/copy/
        // quit/paste/close/cut/redo/undo) minus the letters used by the pin/delete/preview shortcuts,
        // minus pins already in use.
        std::vector<std::wstring> AvailablePins() const;

        // Runs the configured search over pinned and unpinned items separately.
        struct Filtered
        {
            std::vector<SearchResult> pinned;
            std::vector<SearchResult> unpinned;
        };
        Filtered Filter(std::wstring_view query) const;

        Event<> Changed;
        Event<HistoryItemPtr, bool> ItemAdded;             // (item, wasDuplicate)

    private:
        void Resort();
        void Trim();

        void TrimTo(std::size_t maxUnpinned);               // Maccy's limitHistorySize
        void InsertUnpinned(HistoryItemPtr const& item);    // at its sorted position
        void SortPinned();                                  // by PinOrder, stable
        void ReconcilePinOrder();                           // drop unused pins, append missing ones
        void RemovePinFromOrderIfUnused(std::wstring const& pin);
        HistoryItemPtr IsModified(HistoryItem const& item) const;   // Maccy's isModified
        void ForgetInSessionLog(HistoryItemPtr const& item);
        std::vector<std::wstring> SupportedPins() const;
        void OnSettingChanged(std::wstring_view key);

        Storage& m_storage;
        Settings& m_settings;
        Search m_search;
        std::vector<HistoryItemPtr> m_pinned;
        std::vector<HistoryItemPtr> m_unpinned;
        // Items added in this session by clipboard change count (Maccy's sessionLog). Not persisted.
        std::map<std::uint32_t, HistoryItemPtr> m_sessionLog;
        Event<std::wstring_view>::Token m_settingsToken = 0;
    };
}
