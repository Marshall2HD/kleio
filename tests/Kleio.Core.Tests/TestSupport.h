#pragma once

#include "Core/HistoryItem.h"
#include "Core/Settings.h"

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace KleioTests
{
    // In-memory SettingsStore that also counts reads (to verify caching).
    class MemoryStore final : public Kleio::SettingsStore
    {
    public:
        std::optional<std::wstring> Read(std::wstring_view key) const override;
        void Write(std::wstring_view key, std::wstring_view value) override;
        void Remove(std::wstring_view key) override;

        std::map<std::wstring, std::wstring, std::less<>> values;
        mutable int reads = 0;
        int writes = 0;
    };

    // Installs a fresh in-memory store into Settings::Instance() and returns it (owned by Settings).
    MemoryStore& ResetSettings();
    // Same, with pre-populated raw values.
    MemoryStore& ResetSettings(std::map<std::wstring, std::wstring, std::less<>> values);

    // A unique database path in %TEMP%; the files are deleted on destruction.
    class TempDatabase
    {
    public:
        TempDatabase();
        ~TempDatabase();
        TempDatabase(TempDatabase const&) = delete;
        TempDatabase& operator=(TempDatabase const&) = delete;

        std::filesystem::path const& Path() const noexcept { return m_path; }

    private:
        std::filesystem::path m_path;
    };

    // A text item (CF_UNICODETEXT) with a title and deterministic, strictly increasing timestamps.
    Kleio::HistoryItemPtr MakeTextItem(std::wstring_view text);
    Kleio::Timestamp NextTimestamp();

    std::vector<std::uint8_t> Bytes(std::string_view text);
}
