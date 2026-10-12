#include "pch.h"
#include "TestFramework.h"
#include "TestSupport.h"

#include "Core/Settings.h"

using namespace Kleio;
using KleioTests::ResetSettings;

TEST(Settings_Defaults)
{
    ResetSettings();
    auto& s = Settings::Instance();
    CHECK_EQ(s.CheckForUpdates(), true);
    CHECK_EQ(s.PopupShortcut(), (KeyShortcut{ Modifiers::Win | Modifiers::Shift, 'C' }));
    CHECK_EQ(s.PinShortcut(), (KeyShortcut{ Modifiers::Alt, 'P' }));
    CHECK_EQ(s.DeleteShortcut(), (KeyShortcut{ Modifiers::Alt, VK_DELETE }));
    CHECK_EQ(s.TogglePreviewShortcut(), (KeyShortcut{ Modifiers::Ctrl, VK_SPACE }));
    CHECK_EQ(s.SearchMode(), SearchMode::Exact);
    CHECK_EQ(s.PasteByDefault(), false);
    CHECK_EQ(s.RemoveFormattingByDefault(), false);
    CHECK_EQ(s.ReplaceWinV(), false);
    CHECK_EQ(s.PlaySounds(), false);
    CHECK_EQ(s.SaveFiles(), true);
    CHECK_EQ(s.SaveImages(), true);
    CHECK_EQ(s.SaveText(), true);
    CHECK_EQ(s.HistorySize(), 200);
    CHECK_EQ(s.SortBy(), SortBy::LastCopiedAt);
    CHECK_EQ(s.PopupPosition(), PopupPosition::Cursor);
    CHECK_EQ(s.PopupScreen(), 0);
    CHECK_EQ(s.PinTo(), PinsPosition::Top);
    CHECK_EQ(s.ImageMaxHeight(), 40);
    CHECK_EQ(s.OpenPreviewAutomatically(), true);
    CHECK_EQ(s.PreviewDelay(), 1500);
    CHECK_EQ(s.HighlightMatch(), HighlightMatch::Bold);
    CHECK_EQ(s.ShowSpecialSymbols(), true);
    CHECK_EQ(s.ShowTrayIcon(), true);
    CHECK_EQ(s.TrayIcon(), TrayIconStyle::Kleio);
    CHECK_EQ(s.ShowRecentCopyInTray(), false);
    CHECK_EQ(s.ShowSearch(), true);
    CHECK_EQ(s.SearchVisibility(), SearchVisibility::Always);
    CHECK_EQ(s.ShowTitle(), true);
    CHECK_EQ(s.ShowApplicationIcons(), false);
    CHECK_EQ(s.ShowHexColorSwatch(), true);
    CHECK_EQ(s.ShowFooter(), true);
    CHECK_EQ(s.WindowWidth(), 450.0);
    CHECK_EQ(s.WindowHeight(), 800.0);
    CHECK_EQ(s.WindowPositionX(), 0.5);
    CHECK_EQ(s.WindowPositionY(), 0.2);
    CHECK_EQ(s.PreviewWidth(), 400.0);
    CHECK(s.IgnoredApps().empty());
    CHECK_EQ(s.IgnoreAllAppsExceptListed(), false);
    CHECK_EQ(s.IgnoredFormats(), (std::vector<std::wstring>{ L"Kleio.Transient" }));
    CHECK(s.IgnoreRegexps().empty());
    CHECK_EQ(s.IgnoreEvents(), false);
    CHECK_EQ(s.IgnoreOnlyNextEvent(), false);
    CHECK_EQ(s.ClearOnQuit(), false);
    CHECK_EQ(s.ClearSystemClipboard(), false);
    CHECK_EQ(s.SuppressClearAlert(), false);
    CHECK(s.PinOrder().empty());
    CHECK_EQ(s.ShowNotifications(), false);
    CHECK_EQ(s.LastUpdateCheck(), 0.0);
    CHECK_EQ(s.SkippedUpdateVersion(), std::wstring{});
}

TEST(Settings_UpdateState)
{
    auto& store = ResetSettings();
    auto& s = Settings::Instance();
    s.LastUpdateCheck(1767225600.5);
    s.SkippedUpdateVersion(L"1.2.3");
    s.ShowNotifications(true);
    CHECK_EQ(store.values[L"lastUpdateCheck"], std::wstring{ L"1767225600.5" });
    CHECK_EQ(store.values[L"skippedUpdateVersion"], std::wstring{ L"1.2.3" });
    CHECK_EQ(store.values[L"showNotifications"], std::wstring{ L"1" });
    CHECK_EQ(s.LastUpdateCheck(), 1767225600.5);
    CHECK_EQ(s.SkippedUpdateVersion(), std::wstring{ L"1.2.3" });
}

TEST(Settings_Clamping)
{
    ResetSettings();
    auto& s = Settings::Instance();
    s.HistorySize(0);
    CHECK_EQ(s.HistorySize(), 1);
    s.HistorySize(5000);
    CHECK_EQ(s.HistorySize(), 999);
    s.HistorySize(-7);
    CHECK_EQ(s.HistorySize(), 1);
    s.ImageMaxHeight(0);
    CHECK_EQ(s.ImageMaxHeight(), 1);
    s.ImageMaxHeight(500);
    CHECK_EQ(s.ImageMaxHeight(), 200);
    s.PreviewDelay(10);
    CHECK_EQ(s.PreviewDelay(), 200);
    s.PreviewDelay(1'000'000);
    CHECK_EQ(s.PreviewDelay(), 100000);

    // Out-of-range stored values are clamped on read too.
    ResetSettings({ { L"historySize", L"100000" }, { L"imageMaxHeight", L"-3" } });
    CHECK_EQ(Settings::Instance().HistorySize(), 999);
    CHECK_EQ(Settings::Instance().ImageMaxHeight(), 1);
}

TEST(Settings_ChangedRaisedOnlyOnActualChange)
{
    ResetSettings();
    auto& s = Settings::Instance();
    std::vector<std::wstring> changes;
    const auto token = s.Changed.Add([&changes](std::wstring_view key) { changes.emplace_back(key); });

    s.HistorySize(200);           // equals default
    s.PasteByDefault(false);      // equals default
    s.IgnoredFormats({ L"Kleio.Transient" });
    s.PopupShortcut(KeyShortcut{ Modifiers::Win | Modifiers::Shift, 'C' });
    CHECK(changes.empty());

    s.HistorySize(50);
    s.HistorySize(50);
    CHECK_EQ(changes, (std::vector<std::wstring>{ L"historySize" }));

    s.HistorySize(5000);
    s.HistorySize(999);           // already clamped to 999
    CHECK_EQ(changes.size(), std::size_t{ 2 });

    s.SortBy(SortBy::NumberOfCopies);
    s.SortBy(SortBy::NumberOfCopies);
    s.WindowWidth(500.5);
    s.WindowWidth(500.5);
    s.IgnoredApps({ L"KeePassXC.exe" });
    s.IgnoredApps({ L"KeePassXC.exe" });
    s.PinShortcut(KeyShortcut{ Modifiers::Alt, 'K' });
    s.PinShortcut(KeyShortcut{ Modifiers::Alt, 'K' });
    CHECK_EQ(changes, (std::vector<std::wstring>{ L"historySize", L"historySize", L"sortBy", L"windowWidth", L"ignoredApps", L"pinShortcut" }));

    // The key passed to handlers compares equal to the public constants.
    CHECK(changes.back() == Settings::kPinShortcut);
    s.Changed.Remove(token);
}

TEST(Settings_SerializationFormat)
{
    auto& store = ResetSettings();
    auto& s = Settings::Instance();
    s.ClearOnQuit(true);
    s.HistorySize(250);
    s.WindowPositionX(0.25);
    s.PreviewWidth(-12.5);
    s.SearchMode(SearchMode::Regexp);
    s.PopupShortcut(KeyShortcut{ Modifiers::Ctrl | Modifiers::Shift, 'K' });
    s.TogglePreviewShortcut(KeyShortcut{});
    s.IgnoreRegexps({ L"a\"b", L"c\\d", L"line\nbreak", L"" });
    s.PinOrder({ L"b", L"d" });

    CHECK_EQ(store.values[L"clearOnQuit"], std::wstring{ L"1" });
    CHECK_EQ(store.values[L"historySize"], std::wstring{ L"250" });
    CHECK_EQ(store.values[L"windowPositionX"], std::wstring{ L"0.25" });
    CHECK_EQ(store.values[L"previewWidth"], std::wstring{ L"-12.5" });
    CHECK_EQ(store.values[L"searchMode"], std::wstring{ L"2" });
    CHECK_EQ(store.values[L"popupShortcut"], std::wstring{ L"Ctrl+Shift+K" });
    CHECK_EQ(store.values[L"togglePreviewShortcut"], std::wstring{});
    CHECK_EQ(store.values[L"ignoreRegexps"], std::wstring{ L"[\"a\\\"b\",\"c\\\\d\",\"line\\nbreak\",\"\"]" });
    CHECK_EQ(store.values[L"pinOrder"], std::wstring{ L"[\"b\",\"d\"]" });

    s.ClearOnQuit(false);
    CHECK_EQ(store.values[L"clearOnQuit"], std::wstring{ L"0" });

    // Disabled shortcut reads back as empty, not as the default.
    CHECK(s.TogglePreviewShortcut().IsEmpty());
    CHECK_EQ(s.IgnoreRegexps(), (std::vector<std::wstring>{ L"a\"b", L"c\\d", L"line\nbreak", L"" }));
}

TEST(Settings_ReadsExistingValues)
{
    ResetSettings({
        { L"historySize", L"12" },
        { L"saveImages", L"0" },
        { L"windowHeight", L"612.75" },
        { L"pinTo", L"1" },
        { L"popupShortcut", L"Ctrl+Alt+H" },
        { L"ignoredApps", L" [ \"A.exe\" , \"C:\\\\Tools\\\\b.exe\", \"\\u0041\\u00e9\" ] " },
        { L"sortBy", L"9" },                 // out of range -> default
        { L"checkForUpdates", L"maybe" },    // garbage -> default
        { L"previewDelay", L"12abc" },       // garbage -> default
        { L"ignoreRegexps", L"[\"unterminated]" },
        { L"pinShortcut", L"Hyper+Q" },
    });
    auto& s = Settings::Instance();
    CHECK_EQ(s.HistorySize(), 12);
    CHECK_EQ(s.SaveImages(), false);
    CHECK_EQ(s.WindowHeight(), 612.75);
    CHECK_EQ(s.PinTo(), PinsPosition::Bottom);
    CHECK_EQ(s.PopupShortcut(), (KeyShortcut{ Modifiers::Ctrl | Modifiers::Alt, 'H' }));
    CHECK_EQ(s.IgnoredApps(), (std::vector<std::wstring>{ L"A.exe", L"C:\\Tools\\b.exe", L"A\u00e9" }));
    CHECK_EQ(s.SortBy(), SortBy::LastCopiedAt);
    CHECK_EQ(s.CheckForUpdates(), true);
    CHECK_EQ(s.PreviewDelay(), 1500);
    CHECK(s.IgnoreRegexps().empty());
    CHECK_EQ(s.PinShortcut(), (KeyShortcut{ Modifiers::Alt, 'P' }));
}

TEST(Settings_ValuesAreCached)
{
    auto& store = ResetSettings({ { L"historySize", L"42" } });
    auto& s = Settings::Instance();
    CHECK_EQ(s.HistorySize(), 42);
    const int readsAfterFirst = store.reads;
    for (int i = 0; i < 10; ++i)
    {
        CHECK_EQ(s.HistorySize(), 42);
        CHECK_EQ(s.ShowFooter(), true);
    }
    CHECK_EQ(store.reads, readsAfterFirst + 1);   // one extra read for showFooter, then cached

    // Writes go to the store and update the cache.
    s.HistorySize(43);
    CHECK_EQ(store.values[L"historySize"], std::wstring{ L"43" });
    CHECK_EQ(s.HistorySize(), 43);
}

TEST(Settings_ResetWindowPosition)
{
    auto& store = ResetSettings();
    auto& s = Settings::Instance();
    s.WindowPositionX(0.1);
    s.WindowPositionY(0.9);
    std::vector<std::wstring> changes;
    const auto token = s.Changed.Add([&changes](std::wstring_view key) { changes.emplace_back(key); });
    s.ResetWindowPosition();
    CHECK_EQ(s.WindowPositionX(), 0.5);
    CHECK_EQ(s.WindowPositionY(), 0.2);
    CHECK_EQ(changes, (std::vector<std::wstring>{ L"windowPositionX", L"windowPositionY" }));
    CHECK(store.values.find(L"windowPositionX") == store.values.end());
    s.Changed.Remove(token);
}

TEST(Settings_EnumsRoundTrip)
{
    ResetSettings();
    auto& s = Settings::Instance();
    s.PopupPosition(PopupPosition::LastPosition);
    s.HighlightMatch(HighlightMatch::None);
    s.TrayIcon(TrayIconStyle::Scissors);
    s.SearchVisibility(SearchVisibility::DuringSearch);
    s.SearchMode(SearchMode::Mixed);
    CHECK_EQ(s.PopupPosition(), PopupPosition::LastPosition);
    CHECK_EQ(s.HighlightMatch(), HighlightMatch::None);
    CHECK_EQ(s.TrayIcon(), TrayIconStyle::Scissors);
    CHECK_EQ(s.SearchVisibility(), SearchVisibility::DuringSearch);
    CHECK_EQ(s.SearchMode(), SearchMode::Mixed);
}
