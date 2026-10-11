#include "pch.h"
#include "Core/Storage.h"

#include <map>
#include <stdexcept>
#include <string>
#include <system_error>

namespace Kleio
{
    namespace
    {
        void Log(std::wstring const& message)
        {
            OutputDebugStringW((L"Kleio Storage: " + message + L"\n").c_str());
        }

        std::wstring AsciiToWide(char const* text)
        {
            std::wstring result;
            if (!text)
            {
                return result;
            }
            const std::string_view bytes{ text };
            for (char c : bytes)
            {
                result += static_cast<wchar_t>(static_cast<unsigned char>(c));   // diagnostics only
            }
            return result;
        }

        void LogError(sqlite3* db, std::wstring const& what)
        {
            Log(what + L": " + AsciiToWide(db ? sqlite3_errmsg(db) : "no database"));
        }

        // wchar_t is UTF-16 on Windows; convert only where it is not (keeps the code portable).
        std::u16string ToUtf16(std::wstring_view text)
        {
            std::u16string result;
            result.reserve(text.size());
            for (wchar_t c : text)
            {
                auto unit = static_cast<std::uint32_t>(c);
                if (unit >= 0x10000)
                {
                    unit -= 0x10000;
                    result += static_cast<char16_t>(0xD800 + (unit >> 10));
                    result += static_cast<char16_t>(0xDC00 + (unit & 0x3FF));
                }
                else
                {
                    result += static_cast<char16_t>(unit);
                }
            }
            return result;
        }

        std::wstring FromUtf16(char16_t const* text, std::size_t length)
        {
            std::wstring result;
            result.reserve(length);
            for (std::size_t i = 0; i < length; ++i)
            {
                result += static_cast<wchar_t>(text[i]);
            }
            return result;
        }

        // RAII prepared statement.
        class Statement
        {
        public:
            Statement(sqlite3* db, char const* sql) : m_db(db)
            {
                if (sqlite3_prepare_v2(db, sql, -1, &m_stmt, nullptr) != SQLITE_OK)
                {
                    LogError(db, L"prepare failed");
                    m_stmt = nullptr;
                }
            }
            ~Statement()
            {
                if (m_stmt)
                {
                    sqlite3_finalize(m_stmt);
                }
            }
            Statement(Statement const&) = delete;
            Statement& operator=(Statement const&) = delete;

            explicit operator bool() const noexcept { return m_stmt != nullptr; }

            void Reset()
            {
                if (m_stmt)
                {
                    sqlite3_reset(m_stmt);
                    sqlite3_clear_bindings(m_stmt);
                }
                m_failed = false;
            }

            void BindInt64(int index, std::int64_t value)
            {
                if (m_stmt) sqlite3_bind_int64(m_stmt, index, value);
            }
            void BindText(int index, std::wstring const& value)
            {
                if (!m_stmt) return;
                const std::u16string utf16 = ToUtf16(value);
                sqlite3_bind_text16(m_stmt, index, utf16.c_str(), static_cast<int>(utf16.size() * sizeof(char16_t)), SQLITE_TRANSIENT);
            }
            void BindTextOrNull(int index, std::wstring const& value)
            {
                if (!m_stmt) return;
                if (value.empty())
                {
                    sqlite3_bind_null(m_stmt, index);
                }
                else
                {
                    BindText(index, value);
                }
            }
            void BindBlob(int index, std::vector<std::uint8_t> const& value)
            {
                if (!m_stmt) return;
                if (value.empty())
                {
                    sqlite3_bind_zeroblob(m_stmt, index, 0);
                }
                else
                {
                    sqlite3_bind_blob(m_stmt, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
                }
            }

            // SQLITE_ROW -> true; SQLITE_DONE or error -> false.
            bool Step()
            {
                if (!m_stmt)
                {
                    m_failed = true;
                    return false;
                }
                const int rc = sqlite3_step(m_stmt);
                if (rc == SQLITE_ROW)
                {
                    return true;
                }
                if (rc != SQLITE_DONE)
                {
                    m_failed = true;
                    LogError(m_db, L"step failed");
                }
                return false;
            }
            bool Run()
            {
                Step();
                return !m_failed;
            }

            std::int64_t Int64(int column) const { return sqlite3_column_int64(m_stmt, column); }
            std::wstring Text(int column) const
            {
                auto text = static_cast<char16_t const*>(sqlite3_column_text16(m_stmt, column));
                if (!text)
                {
                    return {};
                }
                const int bytes = sqlite3_column_bytes16(m_stmt, column);
                return FromUtf16(text, static_cast<std::size_t>(bytes) / sizeof(char16_t));
            }
            std::vector<std::uint8_t> Blob(int column) const
            {
                auto data = static_cast<std::uint8_t const*>(sqlite3_column_blob(m_stmt, column));
                const int size = sqlite3_column_bytes(m_stmt, column);
                if (!data || size <= 0)
                {
                    return {};
                }
                return std::vector<std::uint8_t>(data, data + size);
            }

        private:
            sqlite3* m_db = nullptr;
            sqlite3_stmt* m_stmt = nullptr;
            bool m_failed = false;
        };

        bool Exec(sqlite3* db, char const* sql)
        {
            char* error = nullptr;
            const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &error);
            if (rc != SQLITE_OK)
            {
                Log(L"exec failed: " + AsciiToWide(error ? error : sqlite3_errmsg(db)));
            }
            if (error)
            {
                sqlite3_free(error);
            }
            return rc == SQLITE_OK;
        }

        // BEGIN IMMEDIATE ... COMMIT, rolled back unless Commit() is called.
        class Transaction
        {
        public:
            explicit Transaction(sqlite3* db) : m_db(db) { m_active = Exec(db, "BEGIN IMMEDIATE"); }
            ~Transaction()
            {
                if (m_active)
                {
                    Exec(m_db, "ROLLBACK");
                }
            }
            Transaction(Transaction const&) = delete;
            Transaction& operator=(Transaction const&) = delete;

            bool Commit()
            {
                if (!m_active)
                {
                    return false;
                }
                m_active = false;
                if (!Exec(m_db, "COMMIT"))
                {
                    Exec(m_db, "ROLLBACK");
                    return false;
                }
                return true;
            }

        private:
            sqlite3* m_db = nullptr;
            bool m_active = false;
        };

        bool InsertContents(sqlite3* db, HistoryItem const& item)
        {
            if (item.contents.empty())
            {
                return true;
            }
            Statement insert(db, "INSERT INTO contents (item_id, type, value) VALUES (?1, ?2, ?3)");
            if (!insert)
            {
                return false;
            }
            for (auto const& content : item.contents)
            {
                insert.Reset();
                insert.BindInt64(1, item.id);
                insert.BindText(2, content.type);
                insert.BindBlob(3, content.value);
                if (!insert.Run())
                {
                    return false;
                }
            }
            return true;
        }
    }

    Storage::Storage(std::filesystem::path const& path) : m_path(path)
    {
        std::error_code ec;
        if (path.has_parent_path())
        {
            std::filesystem::create_directories(path.parent_path(), ec);
        }

        const std::u16string utf16Path = path.u16string();
        const int rc = sqlite3_open16(utf16Path.c_str(), &m_db);
        if (rc != SQLITE_OK)
        {
            std::string message = m_db ? sqlite3_errmsg(m_db) : "out of memory";
            if (m_db)
            {
                sqlite3_close_v2(m_db);
                m_db = nullptr;
            }
            throw std::runtime_error("Cannot open history database: " + message);
        }

        sqlite3_busy_timeout(m_db, 2000);
        Exec(m_db, "PRAGMA foreign_keys = ON");
        Exec(m_db, "PRAGMA journal_mode = WAL");
        Exec(m_db, "PRAGMA synchronous = NORMAL");

        const bool created = Exec(m_db,
            "CREATE TABLE IF NOT EXISTS items ("
            " id INTEGER PRIMARY KEY,"
            " application TEXT,"
            " first_copied_at INTEGER NOT NULL DEFAULT 0,"
            " last_copied_at INTEGER NOT NULL DEFAULT 0,"
            " number_of_copies INTEGER NOT NULL DEFAULT 1,"
            " pin TEXT,"
            " title TEXT"
            ");"
            "CREATE TABLE IF NOT EXISTS contents ("
            " id INTEGER PRIMARY KEY,"
            " item_id INTEGER REFERENCES items(id) ON DELETE CASCADE,"
            " type TEXT,"
            " value BLOB"
            ");"
            "CREATE INDEX IF NOT EXISTS contents_item_id ON contents(item_id);");
        if (!created)
        {
            std::string message = sqlite3_errmsg(m_db);
            sqlite3_close_v2(m_db);
            m_db = nullptr;
            throw std::runtime_error("Cannot create history database schema: " + message);
        }
        // Orphans can only appear if foreign keys were ever off; drop them.
        Exec(m_db, "DELETE FROM contents WHERE item_id IS NULL OR item_id NOT IN (SELECT id FROM items)");
    }

    Storage::~Storage()
    {
        if (m_db)
        {
            sqlite3_close_v2(m_db);
            m_db = nullptr;
        }
    }

    std::vector<HistoryItemPtr> Storage::LoadAll()
    {
        std::vector<HistoryItemPtr> items;
        if (!m_db)
        {
            return items;
        }

        // Newest rows first so that ties in the sort order favour recently inserted items.
        std::map<std::int64_t, HistoryItemPtr> byId;
        {
            Statement select(m_db,
                "SELECT id, application, first_copied_at, last_copied_at, number_of_copies, pin, title "
                "FROM items ORDER BY id DESC");
            while (select.Step())
            {
                auto item = std::make_shared<HistoryItem>();
                item->id = select.Int64(0);
                item->application = select.Text(1);
                item->firstCopiedAt = select.Int64(2);
                item->lastCopiedAt = select.Int64(3);
                item->numberOfCopies = static_cast<std::int32_t>(select.Int64(4));
                item->pin = select.Text(5);
                item->title = select.Text(6);
                byId.emplace(item->id, item);
                items.push_back(std::move(item));
            }
        }
        {
            Statement select(m_db, "SELECT item_id, type, value FROM contents ORDER BY id ASC");
            while (select.Step())
            {
                auto it = byId.find(select.Int64(0));
                if (it == byId.end())
                {
                    continue;
                }
                HistoryItemContent content;
                content.type = select.Text(1);
                content.value = select.Blob(2);
                it->second->contents.push_back(std::move(content));
            }
        }
        return items;
    }

    void Storage::Insert(HistoryItem& item)
    {
        if (!m_db)
        {
            return;
        }
        Transaction transaction(m_db);
        Statement insert(m_db,
            "INSERT INTO items (application, first_copied_at, last_copied_at, number_of_copies, pin, title) "
            "VALUES (?1, ?2, ?3, ?4, ?5, ?6)");
        insert.BindText(1, item.application);
        insert.BindInt64(2, item.firstCopiedAt);
        insert.BindInt64(3, item.lastCopiedAt);
        insert.BindInt64(4, item.numberOfCopies);
        insert.BindTextOrNull(5, item.pin);
        insert.BindText(6, item.title);
        if (!insert.Run())
        {
            return;
        }
        const std::int64_t previousId = item.id;
        item.id = sqlite3_last_insert_rowid(m_db);
        if (!InsertContents(m_db, item) || !transaction.Commit())
        {
            item.id = previousId;
            LogError(m_db, L"insert failed");
        }
    }

    void Storage::Update(HistoryItem const& item)
    {
        if (!m_db || item.id == 0)
        {
            return;
        }
        Statement update(m_db,
            "UPDATE items SET application = ?1, first_copied_at = ?2, last_copied_at = ?3, "
            "number_of_copies = ?4, pin = ?5, title = ?6 WHERE id = ?7");
        update.BindText(1, item.application);
        update.BindInt64(2, item.firstCopiedAt);
        update.BindInt64(3, item.lastCopiedAt);
        update.BindInt64(4, item.numberOfCopies);
        update.BindTextOrNull(5, item.pin);
        update.BindText(6, item.title);
        update.BindInt64(7, item.id);
        update.Run();
    }

    void Storage::UpdateContents(HistoryItem const& item)
    {
        if (!m_db || item.id == 0)
        {
            return;
        }
        Transaction transaction(m_db);
        Statement remove(m_db, "DELETE FROM contents WHERE item_id = ?1");
        remove.BindInt64(1, item.id);
        if (!remove.Run())
        {
            return;
        }
        if (!InsertContents(m_db, item))
        {
            return;
        }
        transaction.Commit();
    }

    void Storage::Remove(std::int64_t id)
    {
        if (!m_db || id == 0)
        {
            return;
        }
        Transaction transaction(m_db);
        Statement removeContents(m_db, "DELETE FROM contents WHERE item_id = ?1");
        removeContents.BindInt64(1, id);
        Statement removeItem(m_db, "DELETE FROM items WHERE id = ?1");
        removeItem.BindInt64(1, id);
        if (removeContents.Run() && removeItem.Run())
        {
            transaction.Commit();
        }
    }

    void Storage::RemoveUnpinned()
    {
        if (!m_db)
        {
            return;
        }
        Transaction transaction(m_db);
        if (Exec(m_db, "DELETE FROM contents WHERE item_id IN (SELECT id FROM items WHERE pin IS NULL OR pin = '')") &&
            Exec(m_db, "DELETE FROM items WHERE pin IS NULL OR pin = ''"))
        {
            transaction.Commit();
        }
    }

    void Storage::RemoveAll()
    {
        if (!m_db)
        {
            return;
        }
        Transaction transaction(m_db);
        if (Exec(m_db, "DELETE FROM contents") && Exec(m_db, "DELETE FROM items"))
        {
            transaction.Commit();
        }
    }

    std::uint64_t Storage::SizeInBytes() const
    {
        std::uint64_t total = 0;
        static constexpr wchar_t const* kSuffixes[] = { L"", L"-wal" };   // database + write-ahead log
        for (auto const* suffix : kSuffixes)
        {
            std::filesystem::path file = m_path;
            file += suffix;
            std::error_code ec;
            const auto size = std::filesystem::file_size(file, ec);
            if (!ec)
            {
                total += static_cast<std::uint64_t>(size);
            }
        }
        return total;
    }
}
