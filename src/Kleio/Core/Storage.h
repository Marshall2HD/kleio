#pragma once

#include "HistoryItem.h"

#include <filesystem>
#include <vector>

struct sqlite3;

namespace Kleio
{
    // SQLite persistence (winsqlite3, ships with Windows) for history items.
    // Schema:
    //   items(id INTEGER PRIMARY KEY, application TEXT, first_copied_at INTEGER, last_copied_at INTEGER,
    //         number_of_copies INTEGER, pin TEXT, title TEXT)
    //   contents(id INTEGER PRIMARY KEY, item_id INTEGER REFERENCES items(id) ON DELETE CASCADE,
    //            type TEXT, value BLOB)
    // Throws std::runtime_error on failures to open; other operations log and return.
    class Storage
    {
    public:
        // Opens/creates the database. Default path: ApplicationData LocalFolder\history.sqlite.
        explicit Storage(std::filesystem::path const& path);
        static std::filesystem::path DefaultPath();
        ~Storage();
        Storage(Storage const&) = delete;
        Storage& operator=(Storage const&) = delete;

        std::vector<HistoryItemPtr> LoadAll();
        void Insert(HistoryItem& item);          // sets item.id, writes contents
        void Update(HistoryItem const& item);    // updates item row (not contents)
        void UpdateContents(HistoryItem const& item); // replaces contents
        void Remove(std::int64_t id);
        void RemoveUnpinned();
        void RemoveAll();
        std::uint64_t SizeInBytes() const;       // database file size (for the Storage settings page)

    private:
        sqlite3* m_db = nullptr;
        std::filesystem::path m_path;
    };
}
