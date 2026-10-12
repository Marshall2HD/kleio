#include "pch.h"
#include "TestFramework.h"
#include "TestSupport.h"

#include "Core/History.h"
#include "Core/Storage.h"

#include <algorithm>

using namespace Kleio;
using KleioTests::Bytes;
using KleioTests::MakeTextItem;

namespace
{
    // Mirrors the setUp of Maccy's HistoryTests: size 10, sort by firstCopiedAt, pins at the bottom.
    struct Fixture
    {
        KleioTests::TempDatabase database;
        std::unique_ptr<Storage> storage;
        std::unique_ptr<History> history;

        Fixture()
        {
            KleioTests::ResetSettings();
            auto& settings = Settings::Instance();
            settings.HistorySize(10);
            settings.SortBy(SortBy::FirstCopiedAt);
            settings.PinTo(PinsPosition::Bottom);
            storage = std::make_unique<Storage>(database.Path());
            history = std::make_unique<History>(*storage, settings);
            history->Load();
        }

        ~Fixture()
        {
            history.reset();
            storage.reset();
        }

        std::vector<HistoryItemPtr> Items() const { return history->AllItems(); }

        bool Contains(HistoryItemPtr const& item) const
        {
            const auto items = Items();
            return std::find(items.begin(), items.end(), item) != items.end();
        }

        std::size_t StoredItems() const { return storage->LoadAll().size(); }

        std::size_t StoredContents() const
        {
            std::size_t count = 0;
            for (auto const& item : storage->LoadAll())
            {
                count += item->contents.size();
            }
            return count;
        }
    };

    HistoryItemPtr ItemWithContents(std::vector<HistoryItemContent> contents, std::wstring application = {})
    {
        auto item = std::make_shared<HistoryItem>();
        item->contents = std::move(contents);
        item->application = std::move(application);
        item->firstCopiedAt = item->lastCopiedAt = KleioTests::NextTimestamp();
        item->title = item->GenerateTitle(true);
        return item;
    }

    HistoryItemContent Text(std::wstring_view text)
    {
        return { std::wstring{ ContentType::Text }, EncodeText(text) };
    }
}

TEST(History_DefaultIsEmpty)
{
    Fixture f;
    CHECK(f.Items().empty());
    CHECK(f.history->MostRecent() == nullptr);
}

TEST(History_Adding)
{
    Fixture f;
    int changed = 0;
    std::vector<std::pair<HistoryItemPtr, bool>> added;
    f.history->Changed.Add([&changed] { ++changed; });
    f.history->ItemAdded.Add([&added](HistoryItemPtr item, bool duplicate) { added.emplace_back(item, duplicate); });

    auto first = f.history->Add(MakeTextItem(L"foo"));
    auto second = f.history->Add(MakeTextItem(L"bar"));
    CHECK(f.Items() == (std::vector<HistoryItemPtr>{ second, first }));
    CHECK(first->id != 0 && second->id != 0 && first->id != second->id);
    CHECK_EQ(changed, 2);
    CHECK_EQ(added.size(), std::size_t{ 2 });
    CHECK(!added.empty() && added[0].first == first && !added[0].second);
    CHECK(f.history->MostRecent() == second);
    CHECK(f.history->FindById(first->id) == first);
    CHECK(f.history->FindById(12345) == nullptr);
    CHECK_EQ(f.StoredItems(), std::size_t{ 2 });
}

TEST(History_AddGeneratesMissingTitle)
{
    Fixture f;
    auto item = std::make_shared<HistoryItem>();
    item->contents.push_back(Text(L"  untitled  "));
    item->firstCopiedAt = item->lastCopiedAt = KleioTests::NextTimestamp();
    f.history->Add(item);
    CHECK_EQ(item->title, std::wstring{ L"\u00B7\u00B7untitled\u00B7\u00B7" });
}

TEST(History_AddingPersistedDuplicate)
{
    Fixture f;
    auto first = MakeTextItem(L"foo");
    first->title = L"xyz";
    first->application = L"C:\\Windows\\System32\\cmd.exe";
    f.history->Add(first);
    f.history->UpdatePin(first, L"f");

    std::vector<bool> duplicates;
    f.history->ItemAdded.Add([&duplicates](HistoryItemPtr, bool duplicate) { duplicates.push_back(duplicate); });

    auto third = MakeTextItem(L"foo");
    third->application = L"C:\\Program Files\\Code\\Code.exe";
    const auto transferredContents = first->contents;
    auto merged = f.history->Add(third);

    CHECK(merged == third);
    CHECK(f.Items() == (std::vector<HistoryItemPtr>{ merged }));
    CHECK(merged->contents == transferredContents);
    CHECK(merged->lastCopiedAt > merged->firstCopiedAt);
    CHECK_EQ(merged->firstCopiedAt, first->firstCopiedAt);
    CHECK_EQ(merged->numberOfCopies, 2);
    CHECK_EQ(merged->pin, std::wstring{ L"f" });
    CHECK_EQ(merged->title, std::wstring{ L"xyz" });
    CHECK_EQ(merged->application, std::wstring{ L"C:\\Windows\\System32\\cmd.exe" });
    CHECK(duplicates == std::vector<bool>{ true });
    CHECK_EQ(f.StoredItems(), std::size_t{ 1 });
    CHECK_EQ(f.StoredContents(), std::size_t{ 1 });

    // Persisted state matches.
    auto stored = f.storage->LoadAll();
    CHECK_EQ(stored.size(), std::size_t{ 1 });
    if (!stored.empty())
    {
        CHECK_EQ(stored[0]->id, merged->id);
        CHECK_EQ(stored[0]->numberOfCopies, 2);
        CHECK_EQ(stored[0]->pin, std::wstring{ L"f" });
        CHECK_EQ(stored[0]->title, std::wstring{ L"xyz" });
        CHECK_EQ(stored[0]->firstCopiedAt, merged->firstCopiedAt);
        CHECK_EQ(stored[0]->lastCopiedAt, merged->lastCopiedAt);
    }
}

TEST(History_AddingItemThatIsSupersededByExisting)
{
    Fixture f;
    const std::vector<HistoryItemContent> firstContents{ Text(L"one"), { std::wstring{ ContentType::Rtf }, Bytes("two") } };
    f.history->Add(ItemWithContents(firstContents, L"Kleio.exe"));
    auto second = f.history->Add(ItemWithContents({ Text(L"one") }, L"Kleio.exe"));

    CHECK(f.Items() == (std::vector<HistoryItemPtr>{ second }));
    CHECK(second->contents == firstContents);
    CHECK_EQ(f.StoredItems(), std::size_t{ 1 });
    CHECK_EQ(f.StoredContents(), firstContents.size());
}

TEST(History_AddingItemFromKleioKeepsOriginalApplication)
{
    Fixture f;
    const std::vector<HistoryItemContent> firstContents{ Text(L"one") };
    f.history->Add(ItemWithContents(firstContents, L"Code.exe"));
    auto second = f.history->Add(ItemWithContents({ Text(L"one"), { std::wstring{ ContentType::FromKleio }, {} } }, L"Kleio.exe"));

    CHECK(f.Items() == (std::vector<HistoryItemPtr>{ second }));
    CHECK_EQ(second->application, std::wstring{ L"Code.exe" });
    CHECK(second->contents == firstContents);
}

TEST(History_DifferentItemsAreNotMerged)
{
    Fixture f;
    f.history->Add(ItemWithContents({ Text(L"one"), { std::wstring{ ContentType::Rtf }, Bytes("rich") } }));
    f.history->Add(ItemWithContents({ Text(L"one"), { std::wstring{ ContentType::Rtf }, Bytes("different") } }));
    CHECK_EQ(f.Items().size(), std::size_t{ 2 });
}

namespace
{
    HistoryItemContent ModifiedMarker(std::string_view changeCount)
    {
        return { std::wstring{ ContentType::Modified }, Bytes(changeCount) };
    }
}

TEST(History_AddingItemWithDifferentModifiedType)
{
    // Port of Maccy's testAddingItemWithDifferentModifiedType: the marker is transient.
    Fixture f;
    const std::vector<HistoryItemContent> firstContents{ Text(L"one"), ModifiedMarker("1") };
    f.history->Add(ItemWithContents(firstContents));
    auto second = f.history->Add(ItemWithContents({ Text(L"one"), ModifiedMarker("2") }));

    CHECK(f.Items() == (std::vector<HistoryItemPtr>{ second }));
    CHECK(second->contents == firstContents);
}

TEST(History_ModifiedAfterCopying)
{
    // Port of Maccy's testModifiedAfterCopying: an app rewrites the copy made at change count 7.
    Fixture f;
    auto foo = MakeTextItem(L"foo");
    foo->changeCount = 7;
    f.history->Add(foo);

    auto bar = ItemWithContents({ Text(L"bar"), ModifiedMarker("7") });
    bar->changeCount = 8;
    auto merged = f.history->Add(bar);

    CHECK(f.Items() == (std::vector<HistoryItemPtr>{ merged }));
    CHECK_EQ(merged->Text(), std::optional<std::wstring>{ L"bar" });   // the modified contents are kept
    CHECK_EQ(merged->numberOfCopies, 2);
    CHECK_EQ(merged->firstCopiedAt, foo->firstCopiedAt);
    CHECK_EQ(f.StoredItems(), std::size_t{ 1 });
    CHECK_EQ(f.StoredContents(), std::size_t{ 2 });

    // Both change counts now lead to the merged item.
    auto again = ItemWithContents({ Text(L"baz"), ModifiedMarker("7") });
    auto mergedAgain = f.history->Add(again);
    CHECK(f.Items() == (std::vector<HistoryItemPtr>{ mergedAgain }));
    CHECK_EQ(mergedAgain->numberOfCopies, 3);
}

TEST(History_ModifiedMarkerOutsideSessionIsANewCopy)
{
    Fixture f;
    auto foo = MakeTextItem(L"foo");
    foo->changeCount = 7;
    f.history->Add(foo);

    // Unknown change count.
    f.history->Add(ItemWithContents({ Text(L"bar"), ModifiedMarker("6") }));
    CHECK_EQ(f.Items().size(), std::size_t{ 2 });

    // Deleted items leave the session log (Maccy's sessionLog.removeValues in delete()).
    f.history->Delete(foo);
    auto baz = f.history->Add(ItemWithContents({ Text(L"baz"), ModifiedMarker("7") }));
    CHECK_EQ(f.Items().size(), std::size_t{ 2 });
    CHECK_EQ(baz->numberOfCopies, 1);

    // So do cleared ones.
    auto qux = MakeTextItem(L"qux");
    qux->changeCount = 9;
    f.history->Add(qux);
    f.history->Clear();
    auto quux = f.history->Add(ItemWithContents({ Text(L"quux"), ModifiedMarker("9") }));
    CHECK(f.Items() == (std::vector<HistoryItemPtr>{ quux }));
    CHECK_EQ(quux->numberOfCopies, 1);
}

TEST(History_DuplicateWithAllFormatsKeepsOriginalContents)
{
    // A FromKleio copy reads back every format Kleio wrote; it must merge into the original.
    Fixture f;
    const std::vector<HistoryItemContent> office{
        Text(L"cell"),
        { std::wstring{ ContentType::Html }, Bytes("<td>cell</td>") },
        { L"Biff12", Bytes("sheet") },
        { L"CF_ENHMETAFILE", Bytes("emf") },
        { L"CanUploadToCloudClipboard", { 1, 0, 0, 0 } },
    };
    f.history->Add(ItemWithContents(office, L"EXCEL.EXE"));
    std::vector<HistoryItemContent> readBack{ office.rbegin(), office.rend() };
    readBack.push_back({ std::wstring{ ContentType::FromKleio }, {} });
    auto copy = f.history->Add(ItemWithContents(readBack, L"Kleio.exe"));

    CHECK(f.Items() == (std::vector<HistoryItemPtr>{ copy }));
    CHECK(copy->contents == office);
    CHECK_EQ(copy->numberOfCopies, 2);
    CHECK_EQ(copy->application, std::wstring{ L"EXCEL.EXE" });
    CHECK_EQ(f.StoredContents(), office.size());
}

TEST(History_ClearingUnpinned)
{
    Fixture f;
    auto pinned = f.history->Add(MakeTextItem(L"foo"));
    f.history->TogglePin(pinned);
    f.history->Add(MakeTextItem(L"bar"));

    f.history->Clear();

    CHECK(f.Items() == (std::vector<HistoryItemPtr>{ pinned }));
    CHECK_EQ(f.StoredItems(), std::size_t{ 1 });
    CHECK_EQ(f.StoredContents(), std::size_t{ 1 });
}

TEST(History_ClearingAll)
{
    Fixture f;
    f.history->Add(MakeTextItem(L"foo"));
    auto pinned = f.history->Add(MakeTextItem(L"bar"));
    f.history->TogglePin(pinned);

    f.history->ClearAll();

    CHECK(f.Items().empty());
    CHECK(Settings::Instance().PinOrder().empty());
    CHECK_EQ(f.StoredItems(), std::size_t{ 0 });
    CHECK_EQ(f.StoredContents(), std::size_t{ 0 });
}

TEST(History_MaxSize)
{
    Fixture f;
    std::vector<HistoryItemPtr> items;
    for (int index = 0; index <= 10; ++index)
    {
        items.push_back(f.history->Add(MakeTextItem(std::to_wstring(index))));
    }
    CHECK_EQ(f.Items().size(), std::size_t{ 10 });
    CHECK(f.Contains(items[10]));
    CHECK(!f.Contains(items[0]));
    CHECK_EQ(f.StoredItems(), std::size_t{ 10 });
    CHECK_EQ(f.StoredContents(), std::size_t{ 10 });
}

TEST(History_MaxSizeIgnoresPinned)
{
    Fixture f;
    std::vector<HistoryItemPtr> items;
    auto item = f.history->Add(MakeTextItem(L"0"));
    items.push_back(item);
    f.history->TogglePin(item);
    for (int index = 1; index <= 11; ++index)
    {
        items.push_back(f.history->Add(MakeTextItem(std::to_wstring(index))));
    }
    CHECK_EQ(f.Items().size(), std::size_t{ 11 });
    CHECK(f.Contains(items[10]));
    CHECK(f.Contains(items[0]));
    CHECK(!f.Contains(items[1]));
}

TEST(History_MaxSizeIsChanged)
{
    Fixture f;
    std::vector<HistoryItemPtr> items;
    for (int index = 0; index <= 10; ++index)
    {
        items.push_back(f.history->Add(MakeTextItem(std::to_wstring(index))));
    }
    Settings::Instance().HistorySize(5);   // History trims on the Changed notification
    CHECK_EQ(f.Items().size(), std::size_t{ 5 });
    f.history->Add(MakeTextItem(L"11"));
    CHECK_EQ(f.Items().size(), std::size_t{ 5 });
    CHECK(f.Contains(items[10]));
    CHECK(!f.Contains(items[5]));
    CHECK_EQ(f.StoredItems(), std::size_t{ 5 });
}

TEST(History_PinningUpdatesPinOrder)
{
    Fixture f;
    auto item = f.history->Add(MakeTextItem(L"foo"));
    f.history->TogglePin(item);
    CHECK(item->IsPinned());
    CHECK(f.history->PinnedItems() == (std::vector<HistoryItemPtr>{ item }));
    CHECK(f.history->UnpinnedItems().empty());
    CHECK_EQ(Settings::Instance().PinOrder(), (std::vector<std::wstring>{ item->pin }));
    const auto stored = f.storage->LoadAll();
    CHECK(!stored.empty() && stored[0]->pin == item->pin);
}

TEST(History_UnpinningUpdatesPinOrder)
{
    Fixture f;
    auto item = f.history->Add(MakeTextItem(L"foo"));
    f.history->TogglePin(item);
    f.history->TogglePin(item);
    CHECK(!item->IsPinned());
    CHECK(f.history->PinnedItems().empty());
    CHECK(f.history->UnpinnedItems() == (std::vector<HistoryItemPtr>{ item }));
    CHECK(Settings::Instance().PinOrder().empty());
}

TEST(History_MovingPinsUpdatesPinOrder)
{
    Fixture f;
    auto first = f.history->Add(MakeTextItem(L"foo"));
    auto second = f.history->Add(MakeTextItem(L"bar"));
    f.history->TogglePin(first);
    f.history->TogglePin(second);
    CHECK(f.history->PinnedItems() == (std::vector<HistoryItemPtr>{ first, second }));

    f.history->MovePin(0, 1);

    CHECK(f.history->PinnedItems() == (std::vector<HistoryItemPtr>{ second, first }));
    CHECK_EQ(Settings::Instance().PinOrder(), (std::vector<std::wstring>{ second->pin, first->pin }));

    // Order survives a reload.
    History reloaded(*f.storage, Settings::Instance());
    reloaded.Load();
    CHECK_EQ(reloaded.PinnedItems().size(), std::size_t{ 2 });
    if (reloaded.PinnedItems().size() == 2)
    {
        CHECK_EQ(reloaded.PinnedItems()[0]->id, second->id);
        CHECK_EQ(reloaded.PinnedItems()[1]->id, first->id);
    }
}

TEST(History_AvailablePins)
{
    Fixture f;
    const std::vector<std::wstring> defaults{ L"b", L"d", L"e", L"f", L"g", L"h", L"i", L"j", L"k", L"l", L"m", L"n", L"o", L"r", L"s", L"t", L"u" };
    CHECK_EQ(f.history->AvailablePins(), defaults);   // "p" is taken by the Alt+P pin shortcut

    Settings::Instance().PinShortcut(KeyShortcut{ Modifiers::Alt, 'K' });
    Settings::Instance().DeleteShortcut(KeyShortcut{ Modifiers::Alt, 'D' });
    auto pins = f.history->AvailablePins();
    CHECK(std::find(pins.begin(), pins.end(), L"p") != pins.end());
    CHECK(std::find(pins.begin(), pins.end(), L"k") == pins.end());
    CHECK(std::find(pins.begin(), pins.end(), L"d") == pins.end());

    auto item = f.history->Add(MakeTextItem(L"foo"));
    f.history->UpdatePin(item, L"b");
    CHECK_EQ(item->pin, std::wstring{ L"b" });
    pins = f.history->AvailablePins();
    CHECK(std::find(pins.begin(), pins.end(), L"b") == pins.end());
    CHECK(std::is_sorted(pins.begin(), pins.end()));
}

TEST(History_UpdatePin)
{
    Fixture f;
    auto first = f.history->Add(MakeTextItem(L"foo"));
    auto second = f.history->Add(MakeTextItem(L"bar"));
    f.history->UpdatePin(first, L"e");
    f.history->UpdatePin(second, L"g");
    CHECK_EQ(Settings::Instance().PinOrder(), (std::vector<std::wstring>{ L"e", L"g" }));

    f.history->UpdatePin(second, L"e");   // taken by another item -> ignored
    CHECK_EQ(second->pin, std::wstring{ L"g" });
    f.history->UpdatePin(second, L"v");   // reserved -> ignored
    CHECK_EQ(second->pin, std::wstring{ L"g" });
    f.history->UpdatePin(second, L"p");   // used by the pin shortcut -> ignored
    CHECK_EQ(second->pin, std::wstring{ L"g" });

    f.history->UpdatePin(first, L"h");    // replaces "e" in place
    CHECK_EQ(Settings::Instance().PinOrder(), (std::vector<std::wstring>{ L"h", L"g" }));
    CHECK(f.history->PinnedItems() == (std::vector<HistoryItemPtr>{ first, second }));
}

TEST(History_PinningWhenNoPinIsAvailable)
{
    Fixture f;
    const auto available = f.history->AvailablePins().size();
    Settings::Instance().HistorySize(100);
    for (std::size_t i = 0; i < available; ++i)
    {
        auto item = f.history->Add(MakeTextItem(L"keyed " + std::to_wstring(i)));
        f.history->TogglePin(item);
        CHECK(item->IsPinned());
    }
    CHECK(f.history->AvailablePins().empty());

    auto extra = f.history->Add(MakeTextItem(L"without shortcut"));
    f.history->TogglePin(extra);
    CHECK(!extra->IsPinned());
    CHECK_EQ(f.history->PinnedItems().size(), available);

    std::vector<std::wstring> pins;
    for (auto const& item : f.history->PinnedItems())
    {
        pins.push_back(item->pin);
    }
    std::sort(pins.begin(), pins.end());
    CHECK(std::adjacent_find(pins.begin(), pins.end()) == pins.end());
}

TEST(History_ReaddingBottomMostPinnedItemAtFullCapacity)
{
    Fixture f;   // pins at the bottom
    auto pinned = f.history->Add(MakeTextItem(L"pinned"));
    f.history->TogglePin(pinned);
    for (int index = 0; index < Settings::Instance().HistorySize(); ++index)
    {
        f.history->Add(MakeTextItem(std::to_wstring(index)));
    }
    CHECK(f.Items().back() == pinned);

    auto readded = f.history->Add(MakeTextItem(L"pinned"));
    CHECK(f.Contains(readded));
    CHECK(!f.Contains(pinned));
    CHECK(readded->IsPinned());
    const auto items = f.Items();
    CHECK_EQ(std::count_if(items.begin(), items.end(), [](HistoryItemPtr const& item) { return item->IsPinned(); }), 1);
    CHECK(items.back() == readded);
    CHECK_EQ(f.history->UnpinnedItems().size(), static_cast<std::size_t>(Settings::Instance().HistorySize()));
}

TEST(History_Removing)
{
    Fixture f;
    auto foo = f.history->Add(MakeTextItem(L"foo"));
    auto bar = f.history->Add(MakeTextItem(L"bar"));
    f.history->Delete(foo);
    CHECK(f.Items() == (std::vector<HistoryItemPtr>{ bar }));
    CHECK_EQ(f.StoredItems(), std::size_t{ 1 });
    CHECK_EQ(f.StoredContents(), std::size_t{ 1 });

    f.history->TogglePin(bar);
    f.history->Delete(bar);
    CHECK(f.Items().empty());
    CHECK(Settings::Instance().PinOrder().empty());
}

TEST(History_PinsPositionAndSortSettings)
{
    Fixture f;
    auto a = f.history->Add(MakeTextItem(L"a"));
    auto b = f.history->Add(MakeTextItem(L"b"));
    auto c = f.history->Add(MakeTextItem(L"c"));
    f.history->TogglePin(a);
    CHECK(f.Items() == (std::vector<HistoryItemPtr>{ c, b, a }));   // pinned at the bottom

    int changed = 0;
    f.history->Changed.Add([&changed] { ++changed; });
    Settings::Instance().PinTo(PinsPosition::Top);
    CHECK(f.Items() == (std::vector<HistoryItemPtr>{ a, c, b }));
    CHECK_EQ(changed, 1);

    b->numberOfCopies = 5;
    Settings::Instance().SortBy(SortBy::NumberOfCopies);
    CHECK(f.Items() == (std::vector<HistoryItemPtr>{ a, b, c }));
    CHECK_EQ(changed, 2);
}

TEST(History_ShowSpecialSymbolsRegeneratesTitles)
{
    Fixture f;
    auto item = f.history->Add(MakeTextItem(L"  spaced\n"));
    CHECK_EQ(item->title, std::wstring{ L"\u00B7\u00B7spaced\u23CE" });
    Settings::Instance().ShowSpecialSymbols(false);
    CHECK_EQ(item->title, std::wstring{ L"spaced" });
    const auto stored = f.storage->LoadAll();
    CHECK(!stored.empty() && stored[0]->title == L"spaced");
    Settings::Instance().ShowSpecialSymbols(true);
}

TEST(History_UpdateTitleAndText)
{
    Fixture f;
    auto item = ItemWithContents({ Text(L"old"), { std::wstring{ ContentType::Html }, Bytes("<b>old</b>") } });
    f.history->Add(item);

    f.history->UpdateTitle(item, L"recognized text");
    CHECK_EQ(item->title, std::wstring{ L"recognized text" });

    f.history->UpdateText(item, L"new text");
    CHECK_EQ(item->Text(), std::optional<std::wstring>{ L"new text" });
    CHECK(!item->Has(ContentType::Html));
    CHECK_EQ(item->title, std::wstring{ L"new text" });

    History reloaded(*f.storage, Settings::Instance());
    reloaded.Load();
    CHECK_EQ(reloaded.AllItems().size(), std::size_t{ 1 });
    if (!reloaded.AllItems().empty())
    {
        auto const stored = reloaded.AllItems()[0];
        CHECK_EQ(stored->Text(), std::optional<std::wstring>{ L"new text" });
        CHECK_EQ(stored->title, std::wstring{ L"new text" });
        CHECK_EQ(stored->contents.size(), std::size_t{ 1 });
    }
}

TEST(History_LoadRestoresOrderAndTrims)
{
    Fixture f;
    std::vector<HistoryItemPtr> items;
    for (int index = 0; index < 5; ++index)
    {
        items.push_back(f.history->Add(MakeTextItem(L"item " + std::to_wstring(index))));
    }
    f.history->TogglePin(items[1]);

    Settings::Instance().HistorySize(3);
    History reloaded(*f.storage, Settings::Instance());
    reloaded.Load();
    CHECK_EQ(reloaded.PinnedItems().size(), std::size_t{ 1 });
    CHECK_EQ(reloaded.UnpinnedItems().size(), std::size_t{ 3 });
    if (reloaded.UnpinnedItems().size() == 3 && reloaded.PinnedItems().size() == 1)
    {
        CHECK_EQ(reloaded.UnpinnedItems()[0]->id, items[4]->id);
        CHECK_EQ(reloaded.UnpinnedItems()[1]->id, items[3]->id);
        CHECK_EQ(reloaded.UnpinnedItems()[2]->id, items[2]->id);
        CHECK_EQ(reloaded.PinnedItems()[0]->id, items[1]->id);
        CHECK_EQ(reloaded.UnpinnedItems()[0]->Text(), std::optional<std::wstring>{ L"item 4" });
    }
}

TEST(History_Filter)
{
    Fixture f;
    auto foo = f.history->Add(MakeTextItem(L"foo bar"));
    auto baz = f.history->Add(MakeTextItem(L"baz"));
    f.history->TogglePin(foo);

    auto filtered = f.history->Filter(L"ba");
    CHECK_EQ(filtered.pinned.size(), std::size_t{ 1 });
    CHECK_EQ(filtered.unpinned.size(), std::size_t{ 1 });
    CHECK(!filtered.unpinned.empty() && filtered.unpinned[0].item == baz);
    CHECK(!filtered.pinned.empty() && filtered.pinned[0].ranges == (std::vector<TextRange>{ TextRange{ 4, 6 } }));

    Settings::Instance().SearchMode(SearchMode::Regexp);
    filtered = f.history->Filter(L"^b");
    CHECK(filtered.pinned.empty());
    CHECK_EQ(filtered.unpinned.size(), std::size_t{ 1 });
}

TEST(Storage_RoundTripAndSize)
{
    KleioTests::TempDatabase database;
    {
        Storage storage(database.Path());
        HistoryItem item;
        item.application = L"C:\\Apps\\\u00C9diteur.exe";
        item.firstCopiedAt = 1;
        item.lastCopiedAt = 2;
        item.numberOfCopies = 3;
        item.pin = L"b";
        item.title = L"t\u00EEtle";
        item.contents.push_back(Text(L"text"));
        item.contents.push_back({ std::wstring{ ContentType::Png }, { 0x89, 0x50, 0x4E, 0x47, 0x00, 0xFF } });
        item.contents.push_back({ std::wstring{ ContentType::FromKleio }, {} });
        storage.Insert(item);
        CHECK(item.id != 0);

        auto loaded = storage.LoadAll();
        CHECK_EQ(loaded.size(), std::size_t{ 1 });
        if (!loaded.empty())
        {
            CHECK_EQ(loaded[0]->id, item.id);
            CHECK_EQ(loaded[0]->application, item.application);
            CHECK_EQ(loaded[0]->firstCopiedAt, Timestamp{ 1 });
            CHECK_EQ(loaded[0]->lastCopiedAt, Timestamp{ 2 });
            CHECK_EQ(loaded[0]->numberOfCopies, 3);
            CHECK_EQ(loaded[0]->pin, std::wstring{ L"b" });
            CHECK_EQ(loaded[0]->title, item.title);
            CHECK(loaded[0]->contents == item.contents);
        }

        item.pin.clear();
        item.title = L"updated";
        storage.Update(item);
        item.contents.resize(1);
        storage.UpdateContents(item);
        loaded = storage.LoadAll();
        CHECK(!loaded.empty() && loaded[0]->pin.empty() && loaded[0]->title == L"updated" && loaded[0]->contents.size() == 1);

        CHECK(storage.SizeInBytes() > 0);

        HistoryItem pinned;
        pinned.pin = L"d";
        pinned.contents.push_back(Text(L"keep"));
        storage.Insert(pinned);
        storage.RemoveUnpinned();
        loaded = storage.LoadAll();
        CHECK(loaded.size() == 1 && loaded[0]->id == pinned.id && loaded[0]->contents.size() == 1);

        storage.Remove(pinned.id);
        CHECK(storage.LoadAll().empty());

        storage.Insert(pinned);
        storage.RemoveAll();
        CHECK(storage.LoadAll().empty());
    }
}

TEST(Storage_ArbitraryFormatNames)
{
    KleioTests::TempDatabase database;
    Storage storage(database.Path());
    HistoryItem item;
    item.contents.push_back(Text(L"text"));
    item.contents.push_back({ L"Art::GVML ClipFormat", Bytes("gvml") });
    item.contents.push_back({ L"CF_ENHMETAFILE", std::vector<std::uint8_t>(70000, 0x5A) });
    item.contents.push_back({ L"Format \u00E9\u4E2D", { 0x00, 0x01, 0x00 } });
    item.contents.push_back({ L"x.nspasteboard.ModifiedType", Bytes("12") });
    storage.Insert(item);
    const auto loaded = storage.LoadAll();
    CHECK(loaded.size() == 1 && loaded[0]->contents == item.contents);
}

TEST(Storage_RemovingUnpersistableContents)
{
    // Port of Maccy's testRemovingUnpersistableContents (runs when the database is opened).
    KleioTests::TempDatabase database;
    HistoryItem item;
    item.contents.push_back(Text(L"live"));
    item.contents.push_back({ L"Ole Private Data", Bytes("ole") });
    item.contents.push_back({ L"CF_DIB", Bytes("dib") });
    item.contents.push_back({ L"Biff12", Bytes("sheet") });
    item.contents.push_back({ std::wstring{ ContentType::FromKleio }, {} });
    {
        Storage storage(database.Path());
        storage.Insert(item);
        CHECK_EQ(storage.RemoveUnpersistableContents(), 2);
        CHECK_EQ(storage.RemoveUnpersistableContents(), 0);
        const auto loaded = storage.LoadAll();
        CHECK(loaded.size() == 1 && loaded[0]->contents.size() == 3);
        storage.Insert(item);
    }
    Storage reopened(database.Path());
    for (auto const& stored : reopened.LoadAll())
    {
        CHECK_EQ(stored->contents.size(), std::size_t{ 3 });
        CHECK(!stored->Has(L"Ole Private Data") && !stored->Has(L"CF_DIB"));
        CHECK(stored->Has(L"Biff12") && stored->Has(ContentType::FromKleio) && stored->Has(ContentType::Text));
    }
}
