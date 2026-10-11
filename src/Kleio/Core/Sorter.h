#pragma once

#include "HistoryItem.h"
#include "Settings.h"

#include <string>
#include <vector>

namespace Kleio
{
    // Port of Maccy's Sorter: unpinned items sorted by `by` (descending), pinned items ordered by
    // `pinOrder` (pins not in pinOrder keep their relative order and go last), pins placed at the
    // top or bottom according to `pinTo`.
    std::vector<HistoryItemPtr> SortItems(std::vector<HistoryItemPtr> items, SortBy by, PinsPosition pinTo,
                                          std::vector<std::wstring> const& pinOrder);

    // Strict-weak "a goes before b" for unpinned items.
    bool SortsBefore(HistoryItem const& a, HistoryItem const& b, SortBy by) noexcept;
}
