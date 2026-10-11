#include "pch.h"
#include "Core/Sorter.h"

#include <algorithm>

namespace Kleio
{
    bool SortsBefore(HistoryItem const& a, HistoryItem const& b, SortBy by) noexcept
    {
        switch (by)
        {
        case SortBy::FirstCopiedAt:
            return a.firstCopiedAt > b.firstCopiedAt;
        case SortBy::NumberOfCopies:
            return a.numberOfCopies > b.numberOfCopies;
        case SortBy::LastCopiedAt:
        default:
            return a.lastCopiedAt > b.lastCopiedAt;
        }
    }

    std::vector<HistoryItemPtr> SortItems(std::vector<HistoryItemPtr> items, SortBy by, PinsPosition pinTo,
                                          std::vector<std::wstring> const& pinOrder)
    {
        std::vector<HistoryItemPtr> unpinned;
        std::vector<HistoryItemPtr> pinned;
        for (auto& item : items)
        {
            if (!item)
            {
                continue;
            }
            (item->IsPinned() ? pinned : unpinned).push_back(std::move(item));
        }

        std::stable_sort(unpinned.begin(), unpinned.end(), [by](HistoryItemPtr const& a, HistoryItemPtr const& b) {
            return SortsBefore(*a, *b, by);
        });

        // Pinned items in pinOrder, then pins that are not in pinOrder in their original order.
        std::vector<HistoryItemPtr> orderedPinned;
        orderedPinned.reserve(pinned.size());
        std::vector<bool> taken(pinned.size(), false);
        std::vector<std::wstring> seen;
        for (auto const& pin : pinOrder)
        {
            if (pin.empty() || std::find(seen.begin(), seen.end(), pin) != seen.end())
            {
                continue;
            }
            seen.push_back(pin);
            for (std::size_t i = 0; i < pinned.size(); ++i)
            {
                if (!taken[i] && pinned[i]->pin == pin)
                {
                    taken[i] = true;
                    orderedPinned.push_back(pinned[i]);
                }
            }
        }
        for (std::size_t i = 0; i < pinned.size(); ++i)
        {
            if (!taken[i])
            {
                orderedPinned.push_back(pinned[i]);
            }
        }

        std::vector<HistoryItemPtr> result;
        result.reserve(unpinned.size() + orderedPinned.size());
        if (pinTo == PinsPosition::Bottom)
        {
            result.insert(result.end(), unpinned.begin(), unpinned.end());
            result.insert(result.end(), orderedPinned.begin(), orderedPinned.end());
        }
        else
        {
            result.insert(result.end(), orderedPinned.begin(), orderedPinned.end());
            result.insert(result.end(), unpinned.begin(), unpinned.end());
        }
        return result;
    }
}
