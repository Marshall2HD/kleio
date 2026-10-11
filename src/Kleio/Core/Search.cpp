#include "pch.h"
#include "Core/Search.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cwctype>
#include <regex>
#include <unordered_map>

namespace Kleio
{
    namespace
    {
        // Locale-independent lower-casing that preserves the string length (so that indices into
        // the lowered string are valid indices into the original one).
        std::wstring Lowercase(std::wstring_view text)
        {
            std::wstring result(text);
            if (text.empty())
            {
                return result;
            }
            if (text.size() < 0x7FFFFFFF)
            {
                const int length = static_cast<int>(text.size());
                const int written = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, text.data(), length,
                                                  result.data(), length, nullptr, nullptr, 0);
                if (written == length)
                {
                    return result;
                }
            }
            result.assign(text);
            for (auto& c : result)
            {
                c = static_cast<wchar_t>(std::towlower(static_cast<std::wint_t>(c)));
            }
            return result;
        }

        // ------------------------------------------------------------------ Fuse (Bitap)
        //
        // Faithful port of krisk/fuse-swift (Fuse.swift, FuseUtilities.swift, String+Fuse.swift) as
        // configured by Maccy: Fuse(threshold: 0.7) with the defaults location 0, distance 100,
        // maxPatternLength 32 (which fuse-swift never enforces), case-insensitive, no tokenizing.
        // Swift's Int is 64 bits and its shifts are "smart" (over-shifting yields 0), so patterns
        // longer than 64 characters keep working for exact substrings only, exactly like Maccy.
        // Strings are indexed in UTF-16 code units (Swift uses Characters).

        constexpr double kThreshold = 0.7;
        constexpr std::int64_t kLocation = 0;
        constexpr std::int64_t kDistance = 100;
        constexpr std::size_t kFuzzySearchLimit = 5000;

        std::uint64_t SmartShiftLeft(std::uint64_t value, std::int64_t shift) noexcept
        {
            if (shift < 0)
            {
                return shift <= -64 ? 0 : value >> static_cast<unsigned>(-shift);
            }
            return shift >= 64 ? 0 : value << static_cast<unsigned>(shift);
        }

        struct FusePattern
        {
            std::wstring text;
            std::int64_t len = 0;
            std::uint64_t mask = 0;
            std::unordered_map<wchar_t, std::uint64_t> alphabet;
        };

        struct FuseResult
        {
            double score = 1.0;
            std::vector<TextRange> ranges;
        };

        std::optional<FusePattern> CreatePattern(std::wstring_view query)
        {
            FusePattern pattern;
            pattern.text = Lowercase(query);
            pattern.len = static_cast<std::int64_t>(pattern.text.size());
            if (pattern.len == 0)
            {
                return std::nullopt;
            }
            pattern.mask = SmartShiftLeft(1, pattern.len - 1);
            for (std::int64_t i = 0; i < pattern.len; ++i)
            {
                pattern.alphabet[pattern.text[static_cast<std::size_t>(i)]] |= SmartShiftLeft(1, pattern.len - i - 1);
            }
            return pattern;
        }

        double CalculateScore(std::int64_t patternLength, std::int64_t e, std::int64_t x, std::int64_t loc, std::int64_t distance) noexcept
        {
            const double accuracy = static_cast<double>(e) / static_cast<double>(patternLength);
            const std::int64_t proximity = x > loc ? x - loc : loc - x;
            if (distance == 0)
            {
                return proximity != 0 ? 1.0 : accuracy;
            }
            return accuracy + static_cast<double>(proximity) / static_cast<double>(distance);
        }

        // String+Fuse index(of:startingFrom:)
        std::optional<std::int64_t> IndexOf(std::wstring const& text, std::wstring const& pattern, std::optional<std::int64_t> position)
        {
            if (!position)
            {
                return std::nullopt;
            }
            if (*position < 0 || static_cast<std::int64_t>(text.size()) < *position)
            {
                return std::nullopt;
            }
            const auto found = text.find(pattern, static_cast<std::size_t>(*position));
            if (found == std::wstring::npos)
            {
                return std::nullopt;
            }
            return static_cast<std::int64_t>(found);
        }

        // FuseUtilities.findRanges, converted from closed ranges to half-open TextRanges (as
        // Maccy's Search does with `upperBound + 1`).
        std::vector<TextRange> FindRanges(std::vector<std::uint8_t> const& mask)
        {
            std::vector<TextRange> ranges;
            std::int64_t start = -1;
            for (std::size_t n = 0; n < mask.size(); ++n)
            {
                const auto bit = mask[n];
                if (start == -1 && bit == 1)
                {
                    start = static_cast<std::int64_t>(n);
                }
                else if (start != -1 && bit == 0)
                {
                    ranges.push_back({ static_cast<std::size_t>(start), n });
                    start = -1;
                }
            }
            if (!mask.empty() && mask.back() == 1)
            {
                ranges.push_back({ static_cast<std::size_t>(start), mask.size() });
            }
            return ranges;
        }

        FuseResult FuseSearchImpl(FusePattern const& pattern, std::wstring_view searchString)
        {
            const std::wstring text = Lowercase(searchString);
            const std::int64_t textLength = static_cast<std::int64_t>(text.size());

            // Exact match
            if (pattern.text == text)
            {
                return { 0.0, { TextRange{ 0, text.size() } } };
            }

            const std::int64_t location = kLocation;
            const std::int64_t distance = kDistance;
            double threshold = kThreshold;

            std::optional<std::int64_t> bestLocation = IndexOf(text, pattern.text, location);

            // A mask of the matches. We'll use to determine all the ranges of the matches
            std::vector<std::uint8_t> matchMask(static_cast<std::size_t>(textLength), 0);

            // Get all exact matches, here for speed up
            auto index = IndexOf(text, pattern.text, bestLocation);
            while (index)
            {
                const std::int64_t i = *index;
                const double score = CalculateScore(pattern.len, 0, i, location, distance);
                threshold = std::min(threshold, score);
                bestLocation = i + pattern.len;
                index = IndexOf(text, pattern.text, bestLocation);

                for (std::int64_t idx = 0; idx < pattern.len; ++idx)
                {
                    matchMask[static_cast<std::size_t>(i + idx)] = 1;
                }
            }

            // Reset the best location
            bestLocation.reset();

            double score = 1.0;
            std::int64_t binMax = pattern.len + textLength;
            std::vector<std::uint64_t> lastBitArr;

            auto at = [](std::vector<std::uint64_t> const& array, std::int64_t position) -> std::uint64_t {
                return (position >= 0 && static_cast<std::size_t>(position) < array.size()) ? array[static_cast<std::size_t>(position)] : 0;
            };

            for (std::int64_t i = 0; i < pattern.len; ++i)
            {
                // Scan for the best match; each iteration allows for one more error.
                // Run a binary search to determine how far from the match location we can stray at
                // this error level.
                std::int64_t binMin = 0;
                std::int64_t binMid = binMax;

                while (binMin < binMid)
                {
                    if (CalculateScore(pattern.len, i, location, location + binMid, distance) <= threshold)
                    {
                        binMin = binMid;
                    }
                    else
                    {
                        binMax = binMid;
                    }
                    binMid = ((binMax - binMin) / 2) + binMin;
                }

                // Use the result from this iteration as the maximum for the next.
                binMax = binMid;
                const std::int64_t start = std::max<std::int64_t>(1, location - binMid + 1);
                const std::int64_t finish = std::min(location + binMid, textLength) + pattern.len;

                // Initialize the bit array
                std::vector<std::uint64_t> bitArr(static_cast<std::size_t>(finish + 2), 0);
                bitArr[static_cast<std::size_t>(finish + 1)] = SmartShiftLeft(1, i) - 1;

                if (start > finish)
                {
                    continue;
                }

                // Swift evaluates `(start...finish).reversed()` once, so later updates of `start`
                // inside the loop do not shorten it.
                for (std::int64_t j = finish; j >= start; --j)
                {
                    const std::int64_t currentLocation = j - 1;

                    std::uint64_t charMatch = 0;
                    if (currentLocation < textLength)
                    {
                        const auto found = pattern.alphabet.find(text[static_cast<std::size_t>(currentLocation)]);
                        if (found != pattern.alphabet.end())
                        {
                            charMatch = found->second;
                        }
                    }

                    // A match is found
                    if (charMatch != 0)
                    {
                        matchMask[static_cast<std::size_t>(currentLocation)] = 1;
                    }

                    const auto uj = static_cast<std::size_t>(j);
                    // First pass: exact match
                    bitArr[uj] = ((bitArr[uj + 1] << 1) | 1) & charMatch;

                    // Subsequent passes: fuzzy match
                    if (i > 0)
                    {
                        bitArr[uj] |= (((at(lastBitArr, j + 1) | at(lastBitArr, j)) << 1) | 1) | at(lastBitArr, j + 1);
                    }

                    if ((bitArr[uj] & pattern.mask) != 0)
                    {
                        score = CalculateScore(pattern.len, i, location, currentLocation, distance);

                        // This match will almost certainly be better than any existing match. But check anyway.
                        if (score <= threshold)
                        {
                            // Indeed it is
                            threshold = score;
                            bestLocation = currentLocation;

                            if (!(currentLocation > location))
                            {
                                // Already passed `location`. No point in continuing.
                                break;
                            }
                        }
                    }
                }

                // No hope for a better match at greater error levels
                if (CalculateScore(pattern.len, i + 1, location, location, distance) > threshold)
                {
                    break;
                }

                lastBitArr = std::move(bitArr);
            }

            return { score, FindRanges(matchMask) };
        }

        std::optional<FuseResult> FuseSearch(FusePattern const& pattern, std::wstring_view text)
        {
            auto result = FuseSearchImpl(pattern, text);
            if (result.score == 1.0)
            {
                return std::nullopt;
            }
            return result;
        }
    }

    std::vector<SearchResult> Search::Run(std::wstring_view query, std::vector<HistoryItemPtr> const& within, SearchMode mode) const
    {
        if (query.empty())
        {
            std::vector<SearchResult> results;
            results.reserve(within.size());
            for (auto const& item : within)
            {
                results.push_back(SearchResult{ item, std::nullopt, {} });
            }
            return results;
        }

        switch (mode)
        {
        case SearchMode::Mixed:
            return Mixed(query, within);
        case SearchMode::Regexp:
            return Regexp(query, within);
        case SearchMode::Fuzzy:
            return Fuzzy(query, within);
        case SearchMode::Exact:
        default:
            return Exact(query, within);
        }
    }

    std::vector<SearchResult> Search::Exact(std::wstring_view query, std::vector<HistoryItemPtr> const& within)
    {
        std::vector<SearchResult> results;
        if (query.empty())
        {
            for (auto const& item : within)
            {
                results.push_back(SearchResult{ item, std::nullopt, {} });
            }
            return results;
        }
        const std::wstring needle = Lowercase(query);
        for (auto const& item : within)
        {
            if (!item)
            {
                continue;
            }
            const std::wstring haystack = Lowercase(item->title);
            const auto found = haystack.find(needle);
            if (found != std::wstring::npos)
            {
                results.push_back(SearchResult{ item, std::nullopt, { TextRange{ found, found + needle.size() } } });
            }
        }
        return results;
    }

    std::vector<SearchResult> Search::Regexp(std::wstring_view query, std::vector<HistoryItemPtr> const& within)
    {
        std::vector<SearchResult> results;
        if (query.empty())
        {
            for (auto const& item : within)
            {
                results.push_back(SearchResult{ item, std::nullopt, {} });
            }
            return results;
        }
        try
        {
            const std::wregex regex(query.begin(), query.end(), std::regex_constants::ECMAScript);
            for (auto const& item : within)
            {
                if (!item)
                {
                    continue;
                }
                try
                {
                    std::wsmatch match;
                    if (std::regex_search(item->title, match, regex))
                    {
                        const auto start = static_cast<std::size_t>(match.position(0));
                        const auto length = static_cast<std::size_t>(match.length(0));
                        results.push_back(SearchResult{ item, std::nullopt, { TextRange{ start, start + length } } });
                    }
                }
                catch (std::exception const&)
                {
                    // e.g. regex_error(error_complexity / error_stack) on pathological input.
                }
            }
        }
        catch (std::exception const&)
        {
            return {};   // invalid pattern
        }
        return results;
    }

    std::vector<SearchResult> Search::Fuzzy(std::wstring_view query, std::vector<HistoryItemPtr> const& within)
    {
        std::vector<SearchResult> results;
        const auto pattern = CreatePattern(query);
        if (!pattern)
        {
            for (auto const& item : within)
            {
                results.push_back(SearchResult{ item, std::nullopt, {} });
            }
            return results;
        }

        for (auto const& item : within)
        {
            if (!item)
            {
                continue;
            }
            std::wstring_view searchString = item->title;
            if (searchString.size() > kFuzzySearchLimit)
            {
                // shortcut to avoid slow search (Maccy keeps `searchString[...stopIndex]`)
                searchString = searchString.substr(0, kFuzzySearchLimit + 1);
            }
            if (auto fuzzy = FuseSearch(*pattern, searchString))
            {
                results.push_back(SearchResult{ item, fuzzy->score, std::move(fuzzy->ranges) });
            }
        }

        std::stable_sort(results.begin(), results.end(), [](SearchResult const& a, SearchResult const& b) {
            return a.score.value_or(0.0) < b.score.value_or(0.0);
        });
        return results;
    }

    std::vector<SearchResult> Search::Mixed(std::wstring_view query, std::vector<HistoryItemPtr> const& within)
    {
        auto results = Exact(query, within);
        if (!results.empty())
        {
            return results;
        }
        results = Regexp(query, within);
        if (!results.empty())
        {
            return results;
        }
        return Fuzzy(query, within);
    }
}
