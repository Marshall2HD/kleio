#include "pch.h"
#include "TestFramework.h"
#include "TestSupport.h"

#include "Core/Search.h"
#include "Core/Sorter.h"

using namespace Kleio;
using KleioTests::MakeTextItem;

namespace
{
    // The three items of Maccy's SearchTests.
    std::vector<HistoryItemPtr> MaccyItems()
    {
        return { MakeTextItem(L"foo bar baz"), MakeTextItem(L"foo bar zaz"), MakeTextItem(L"xxx yyy zzz") };
    }

    // Maccy's range(from:to:) helper: closed [from, to] -> half-open TextRange.
    TextRange R(std::size_t from, std::size_t to)
    {
        return TextRange{ from, to + 1 };
    }

    struct Expected
    {
        std::size_t index;
        std::optional<double> score;
        std::vector<TextRange> ranges;
    };

    void CheckResults(std::vector<SearchResult> const& actual, std::vector<HistoryItemPtr> const& items, std::vector<Expected> const& expected)
    {
        CHECK_EQ(actual.size(), expected.size());
        for (std::size_t i = 0; i < std::min(actual.size(), expected.size()); ++i)
        {
            CHECK(actual[i].item == items[expected[i].index]);
            CHECK_EQ(actual[i].score.has_value(), expected[i].score.has_value());
            if (actual[i].score && expected[i].score)
            {
                CHECK_NEAR(*actual[i].score, *expected[i].score, 1e-9);
            }
            CHECK_EQ(actual[i].ranges, expected[i].ranges);
        }
    }
}

TEST(Search_EmptyQueryReturnsEverything)
{
    const auto items = MaccyItems();
    for (auto mode : { SearchMode::Exact, SearchMode::Fuzzy, SearchMode::Regexp, SearchMode::Mixed })
    {
        CheckResults(Search{}.Run(L"", items, mode), items, { { 0, std::nullopt, {} }, { 1, std::nullopt, {} }, { 2, std::nullopt, {} } });
    }
}

TEST(Search_Exact)
{
    const auto items = MaccyItems();
    auto search = [&](std::wstring_view query) { return Search{}.Run(query, items, SearchMode::Exact); };
    CheckResults(search(L"z"), items, { { 0, std::nullopt, { R(10, 10) } }, { 1, std::nullopt, { R(8, 8) } }, { 2, std::nullopt, { R(8, 8) } } });
    CheckResults(search(L"foo"), items, { { 0, std::nullopt, { R(0, 2) } }, { 1, std::nullopt, { R(0, 2) } } });
    CheckResults(search(L"za"), items, { { 1, std::nullopt, { R(8, 9) } } });
    CheckResults(search(L"yyy"), items, { { 2, std::nullopt, { R(4, 6) } } });
    CheckResults(search(L"fbb"), items, {});
    CheckResults(search(L"m"), items, {});
    // Case-insensitive.
    CheckResults(search(L"FOO B"), items, { { 0, std::nullopt, { R(0, 4) } }, { 1, std::nullopt, { R(0, 4) } } });
}

TEST(Search_Fuzzy_MaccyVectors)
{
    const auto items = MaccyItems();
    auto search = [&](std::wstring_view query) { return Search{}.Run(query, items, SearchMode::Fuzzy); };
    CheckResults(search(L"z"), items, {
        { 1, 0.08, { R(8, 8), R(10, 10) } },
        { 2, 0.08, { R(8, 10) } },
        { 0, 0.1, { R(10, 10) } },
    });
    CheckResults(search(L"foo"), items, {
        { 0, 0.0, { R(0, 2) } },
        { 1, 0.0, { R(0, 2) } },
    });
    CheckResults(search(L"za"), items, {
        { 1, 0.08, { R(5, 5), R(8, 9) } },
        { 0, 0.54, { R(5, 5), R(9, 10) } },
        { 2, 0.58, { R(8, 10) } },
    });
    CheckResults(search(L"yyy"), items, { { 2, 0.04, { R(4, 6) } } });
    CheckResults(search(L"fbb"), items, {
        { 0, 0.6666666666666666, { R(0, 0), R(4, 4), R(8, 8) } },
        { 1, 0.6666666666666666, { R(0, 0), R(4, 4) } },
    });
    CheckResults(search(L"m"), items, {});
}

TEST(Search_Fuzzy_CaseInsensitiveAndExactMatch)
{
    const std::vector<HistoryItemPtr> items{ MakeTextItem(L"Hello") };
    CheckResults(Search::Fuzzy(L"hello", items), items, { { 0, 0.0, { R(0, 4) } } });
    CheckResults(Search::Fuzzy(L"HELLO", items), items, { { 0, 0.0, { R(0, 4) } } });
}

TEST(Search_Fuzzy_LongPatterns)
{
    // Patterns longer than 32 characters keep working (fuse-swift never enforces maxPatternLength
    // and uses 64-bit masks).
    const std::wstring sentence = L"the quick brown fox jumps over the lazy dog";
    const std::vector<HistoryItemPtr> items{ MakeTextItem(sentence), MakeTextItem(L"something else entirely") };
    const auto typo = Search::Fuzzy(L"the quikc brown fox jumps over the lazy", items);   // 39 chars
    CHECK(!typo.empty());
    if (!typo.empty())
    {
        CHECK(typo[0].item == items[0]);
        CHECK(typo[0].score.has_value() && *typo[0].score < 0.7);
    }

    // > 64 characters: only a full-string match can succeed (Swift's smart shifts produce a 0 mask).
    const std::wstring longText(70, L'k');
    const std::vector<HistoryItemPtr> longItems{ MakeTextItem(longText), MakeTextItem(longText + L"x") };
    CheckResults(Search::Fuzzy(longText, longItems), longItems, { { 0, 0.0, { TextRange{ 0, 70 } } } });
}

TEST(Search_Fuzzy_RespectsLimit)
{
    std::wstring text(6000, L'a');
    text += L"needle";
    const std::vector<HistoryItemPtr> items{ std::make_shared<HistoryItem>() };
    items[0]->title = text;   // titles can exceed 1000 characters when set by OCR
    CHECK(Search::Fuzzy(L"needle", items).empty());
}

TEST(Search_Regexp)
{
    const auto items = MaccyItems();
    auto search = [&](std::wstring_view query) { return Search{}.Run(query, items, SearchMode::Regexp); };
    CheckResults(search(L"z+"), items, { { 0, std::nullopt, { R(10, 10) } }, { 1, std::nullopt, { R(8, 8) } }, { 2, std::nullopt, { R(8, 10) } } });
    CheckResults(search(L"z*"), items, {
        { 0, std::nullopt, { TextRange{ 0, 0 } } },
        { 1, std::nullopt, { TextRange{ 0, 0 } } },
        { 2, std::nullopt, { TextRange{ 0, 0 } } },
    });
    CheckResults(search(L"^foo"), items, { { 0, std::nullopt, { R(0, 2) } }, { 1, std::nullopt, { R(0, 2) } } });
    CheckResults(search(L" za"), items, { { 1, std::nullopt, { R(7, 9) } } });
    CheckResults(search(L"[y]+"), items, { { 2, std::nullopt, { R(4, 6) } } });
    CheckResults(search(L"fbb"), items, {});
    CheckResults(search(L"m"), items, {});
    // Case-sensitive, invalid patterns yield nothing.
    CheckResults(search(L"FOO"), items, {});
    CheckResults(search(L"("), items, {});
    CheckResults(search(L"[a-"), items, {});
}

TEST(Search_Mixed)
{
    const auto items = MaccyItems();
    auto search = [&](std::wstring_view query) { return Search{}.Run(query, items, SearchMode::Mixed); };
    // Exact wins when it finds anything.
    CheckResults(search(L"foo"), items, { { 0, std::nullopt, { R(0, 2) } }, { 1, std::nullopt, { R(0, 2) } } });
    // No exact match for "z+" -> regexp.
    CheckResults(search(L"z+"), items, { { 0, std::nullopt, { R(10, 10) } }, { 1, std::nullopt, { R(8, 8) } }, { 2, std::nullopt, { R(8, 10) } } });
    // Neither exact nor regexp -> fuzzy.
    const auto fuzzy = search(L"fbb");
    CHECK_EQ(fuzzy.size(), std::size_t{ 2 });
    CHECK(!fuzzy.empty() && fuzzy[0].score.has_value());
    // Invalid regexp falls through to fuzzy.
    CHECK(search(L"(").empty());
}

// ---- Sorter (ports of Maccy's SorterTests) ----

namespace
{
    constexpr Timestamp kSorterNow = 1'800'000'000'000;

    struct SorterFixture
    {
        HistoryItemPtr item1;
        HistoryItemPtr item2;
        HistoryItemPtr item3;

        SorterFixture()
        {
            auto make = [](std::wstring_view text, int firstCopiedAt, int lastCopiedAt, int copies) {
                auto item = MakeTextItem(text);
                item->firstCopiedAt = kSorterNow + firstCopiedAt * 1000;
                item->lastCopiedAt = kSorterNow + lastCopiedAt * 1000;
                item->numberOfCopies = copies;
                return item;
            };
            item1 = make(L"foo", -300, -100, 3);
            item2 = make(L"bar", -400, -300, 2);
            item3 = make(L"bar", -200, -200, 1);
        }

        std::vector<HistoryItemPtr> All() const { return { item1, item2, item3 }; }
    };
}

TEST(Sorter_ByLastCopiedAt)
{
    SorterFixture f;
    CHECK(SortItems(f.All(), SortBy::LastCopiedAt, PinsPosition::Top, {}) == (std::vector<HistoryItemPtr>{ f.item1, f.item3, f.item2 }));
}

TEST(Sorter_ByFirstCopiedAt)
{
    SorterFixture f;
    CHECK(SortItems(f.All(), SortBy::FirstCopiedAt, PinsPosition::Top, {}) == (std::vector<HistoryItemPtr>{ f.item3, f.item1, f.item2 }));
}

TEST(Sorter_ByNumberOfCopies)
{
    SorterFixture f;
    CHECK(SortItems(f.All(), SortBy::NumberOfCopies, PinsPosition::Top, {}) == (std::vector<HistoryItemPtr>{ f.item1, f.item2, f.item3 }));
}

TEST(Sorter_PinsToTopAndBottom)
{
    SorterFixture f;
    f.item1->pin = L"b";
    f.item3->pin = L"d";
    CHECK(SortItems(f.All(), SortBy::LastCopiedAt, PinsPosition::Top, {}) == (std::vector<HistoryItemPtr>{ f.item1, f.item3, f.item2 }));
    CHECK(SortItems(f.All(), SortBy::LastCopiedAt, PinsPosition::Bottom, {}) == (std::vector<HistoryItemPtr>{ f.item2, f.item1, f.item3 }));
}

TEST(Sorter_PinOrder)
{
    SorterFixture f;
    f.item1->pin = L"b";
    f.item2->pin = L"e";
    f.item3->pin = L"d";
    // Pins in pinOrder first (in that order), the rest keep their relative order.
    CHECK(SortItems(f.All(), SortBy::LastCopiedAt, PinsPosition::Top, { L"d", L"b" }) ==
          (std::vector<HistoryItemPtr>{ f.item3, f.item1, f.item2 }));
    CHECK(SortItems(f.All(), SortBy::LastCopiedAt, PinsPosition::Top, { L"e", L"x", L"d", L"e" }) ==
          (std::vector<HistoryItemPtr>{ f.item2, f.item3, f.item1 }));
    CHECK(SortsBefore(*f.item1, *f.item2, SortBy::LastCopiedAt));
    CHECK(!SortsBefore(*f.item2, *f.item1, SortBy::LastCopiedAt));
    CHECK(!SortsBefore(*f.item1, *f.item1, SortBy::NumberOfCopies));
}
