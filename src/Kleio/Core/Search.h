#pragma once

#include "HistoryItem.h"
#include "Settings.h"

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace Kleio
{
    // [start, end) range in UTF-16 code units of the searched title.
    struct TextRange
    {
        std::size_t start = 0;
        std::size_t end = 0;
        bool operator==(TextRange const&) const = default;
    };

    struct SearchResult
    {
        HistoryItemPtr item;
        std::optional<double> score;     // fuzzy only; lower is better
        std::vector<TextRange> ranges;   // highlighted ranges within item->title
    };

    // Port of Maccy's Search: exact (case-insensitive substring), regexp (ECMAScript, case-sensitive),
    // fuzzy (Bitap-based, Fuse.js-like, threshold 0.7, first 5000 chars, results sorted by score)
    // and mixed (exact, then regexp, then fuzzy; first non-empty wins).
    // Searches item->title. An empty query returns all items with no ranges, preserving order.
    class Search
    {
    public:
        std::vector<SearchResult> Run(std::wstring_view query, std::vector<HistoryItemPtr> const& within, SearchMode mode) const;

        static std::vector<SearchResult> Exact(std::wstring_view query, std::vector<HistoryItemPtr> const& within);
        static std::vector<SearchResult> Regexp(std::wstring_view query, std::vector<HistoryItemPtr> const& within);
        static std::vector<SearchResult> Fuzzy(std::wstring_view query, std::vector<HistoryItemPtr> const& within);
        static std::vector<SearchResult> Mixed(std::wstring_view query, std::vector<HistoryItemPtr> const& within);
    };
}
