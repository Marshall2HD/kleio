#pragma once

#include "Event.h"
#include "KeyShortcut.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Kleio
{
    enum class SearchMode : std::int32_t { Exact = 0, Fuzzy = 1, Regexp = 2, Mixed = 3 };
    enum class PinsPosition : std::int32_t { Top = 0, Bottom = 1 };
    // Where the popup opens. Mirrors Maccy's PopupPosition (statusItem -> tray icon).
    enum class PopupPosition : std::int32_t { Cursor = 0, TrayIcon = 1, ActiveWindow = 2, ScreenCenter = 3, LastPosition = 4 };
    enum class SearchVisibility : std::int32_t { Always = 0, DuringSearch = 1 };
    enum class SortBy : std::int32_t { LastCopiedAt = 0, FirstCopiedAt = 1, NumberOfCopies = 2 };
    enum class HighlightMatch : std::int32_t { Bold = 0, Italic = 1, Underline = 2, Color = 3, None = 4 };
    // Tray icon glyph. Mirrors Maccy's MenuIcon.
    enum class TrayIconStyle : std::int32_t { Kleio = 0, Clipboard = 1, Paperclip = 2, Scissors = 3 };

    // Clipboard formats that are always ignored regardless of user settings: they are the Windows
    // conventions for "do not record this" (password managers, etc.).
    inline constexpr std::wstring_view AlwaysIgnoredFormats[] = {
        L"ExcludeClipboardContentFromMonitorProcessing",
        L"Clipboard Viewer Ignore",
    };

    // Persistence backend for Settings. The app uses the ApplicationData LocalSettings backend;
    // unit tests install an in-memory one via Settings::UseStore before first use.
    class SettingsStore
    {
    public:
        virtual ~SettingsStore() = default;
        // Values are stored as strings; Settings handles (de)serialization.
        virtual std::optional<std::wstring> Read(std::wstring_view key) const = 0;
        virtual void Write(std::wstring_view key, std::wstring_view value) = 0;
        virtual void Remove(std::wstring_view key) = 0;
    };

    // The production backend (ApplicationData::Current().LocalSettings(), falls back to an in-memory
    // store when package identity is unavailable). Defined in Core/AppData.cpp.
    std::unique_ptr<SettingsStore> CreateDefaultSettingsStore();

    // Strongly typed application settings, persisted in ApplicationData::Current().LocalSettings().
    // Every setter persists immediately and raises Changed(key) with the key constant below when the
    // value actually changed. Must be used from the UI thread.
    class Settings
    {
    public:
        static Settings& Instance();
        // Replaces the persistence backend (tests only). Default: LocalSettings backend.
        static void UseStore(std::unique_ptr<SettingsStore> store);

        Event<std::wstring_view> Changed;

        // ---- Keys (also the LocalSettings value names) ----
        static constexpr std::wstring_view kCheckForUpdates = L"checkForUpdates";
        static constexpr std::wstring_view kPopupShortcut = L"popupShortcut";
        static constexpr std::wstring_view kPinShortcut = L"pinShortcut";
        static constexpr std::wstring_view kDeleteShortcut = L"deleteShortcut";
        static constexpr std::wstring_view kTogglePreviewShortcut = L"togglePreviewShortcut";
        static constexpr std::wstring_view kSearchMode = L"searchMode";
        static constexpr std::wstring_view kPasteByDefault = L"pasteByDefault";
        static constexpr std::wstring_view kRemoveFormattingByDefault = L"removeFormattingByDefault";
        static constexpr std::wstring_view kReplaceWinV = L"replaceWinV";
        static constexpr std::wstring_view kSaveFiles = L"saveFiles";
        static constexpr std::wstring_view kSaveImages = L"saveImages";
        static constexpr std::wstring_view kSaveText = L"saveText";
        static constexpr std::wstring_view kHistorySize = L"historySize";
        static constexpr std::wstring_view kSortBy = L"sortBy";
        static constexpr std::wstring_view kPopupPosition = L"popupPosition";
        static constexpr std::wstring_view kPopupScreen = L"popupScreen";
        static constexpr std::wstring_view kPinTo = L"pinTo";
        static constexpr std::wstring_view kImageMaxHeight = L"imageMaxHeight";
        static constexpr std::wstring_view kOpenPreviewAutomatically = L"openPreviewAutomatically";
        static constexpr std::wstring_view kPreviewDelay = L"previewDelay";
        static constexpr std::wstring_view kHighlightMatch = L"highlightMatch";
        static constexpr std::wstring_view kShowSpecialSymbols = L"showSpecialSymbols";
        static constexpr std::wstring_view kShowTrayIcon = L"showTrayIcon";
        static constexpr std::wstring_view kTrayIcon = L"trayIcon";
        static constexpr std::wstring_view kShowRecentCopyInTray = L"showRecentCopyInTray";
        static constexpr std::wstring_view kShowSearch = L"showSearch";
        static constexpr std::wstring_view kSearchVisibility = L"searchVisibility";
        static constexpr std::wstring_view kShowTitle = L"showTitle";
        static constexpr std::wstring_view kShowApplicationIcons = L"showApplicationIcons";
        static constexpr std::wstring_view kShowHexColorSwatch = L"showHexColorSwatch";
        static constexpr std::wstring_view kShowFooter = L"showFooter";
        static constexpr std::wstring_view kWindowWidth = L"windowWidth";
        static constexpr std::wstring_view kWindowHeight = L"windowHeight";
        static constexpr std::wstring_view kWindowPositionX = L"windowPositionX";
        static constexpr std::wstring_view kWindowPositionY = L"windowPositionY";
        static constexpr std::wstring_view kPreviewWidth = L"previewWidth";
        static constexpr std::wstring_view kIgnoredApps = L"ignoredApps";
        static constexpr std::wstring_view kIgnoreAllAppsExceptListed = L"ignoreAllAppsExceptListed";
        static constexpr std::wstring_view kIgnoredFormats = L"ignoredFormats";
        static constexpr std::wstring_view kIgnoreRegexps = L"ignoreRegexps";
        static constexpr std::wstring_view kIgnoreEvents = L"ignoreEvents";
        static constexpr std::wstring_view kIgnoreOnlyNextEvent = L"ignoreOnlyNextEvent";
        static constexpr std::wstring_view kClearOnQuit = L"clearOnQuit";
        static constexpr std::wstring_view kClearSystemClipboard = L"clearSystemClipboard";
        static constexpr std::wstring_view kSuppressClearAlert = L"suppressClearAlert";
        static constexpr std::wstring_view kPinOrder = L"pinOrder";
        static constexpr std::wstring_view kPlaySounds = L"playSounds";

        // ---- General ----
        bool CheckForUpdates() const;                    // default true
        void CheckForUpdates(bool value);
        KeyShortcut PopupShortcut() const;               // default Win+Alt+V (empty = disabled)
        void PopupShortcut(KeyShortcut value);
        KeyShortcut PinShortcut() const;                 // default Alt+P
        void PinShortcut(KeyShortcut value);
        KeyShortcut DeleteShortcut() const;              // default Alt+Delete
        void DeleteShortcut(KeyShortcut value);
        KeyShortcut TogglePreviewShortcut() const;       // default Ctrl+Space
        void TogglePreviewShortcut(KeyShortcut value);
        Kleio::SearchMode SearchMode() const;            // default Exact
        void SearchMode(Kleio::SearchMode value);
        bool PasteByDefault() const;                     // default false
        void PasteByDefault(bool value);
        bool RemoveFormattingByDefault() const;          // default false
        void RemoveFormattingByDefault(bool value);
        // When on, Kleio intercepts Win+V (low-level keyboard hook) and opens its own popup
        // instead of the Windows clipboard history. Default false.
        bool ReplaceWinV() const;
        void ReplaceWinV(bool value);
        bool PlaySounds() const;                         // default false (Maccy's copy "knock"/"write" sounds)
        void PlaySounds(bool value);

        // ---- Storage ----
        bool SaveFiles() const;                          // default true
        void SaveFiles(bool value);
        bool SaveImages() const;                         // default true
        void SaveImages(bool value);
        bool SaveText() const;                           // default true
        void SaveText(bool value);
        std::int32_t HistorySize() const;                // default 200, clamped to [1, 999]
        void HistorySize(std::int32_t value);
        Kleio::SortBy SortBy() const;                    // default LastCopiedAt
        void SortBy(Kleio::SortBy value);

        // ---- Appearance ----
        Kleio::PopupPosition PopupPosition() const;      // default Cursor
        void PopupPosition(Kleio::PopupPosition value);
        std::int32_t PopupScreen() const;                // 0 = active screen, N = N-th monitor (1-based). default 0
        void PopupScreen(std::int32_t value);
        PinsPosition PinTo() const;                      // default Top
        void PinTo(PinsPosition value);
        std::int32_t ImageMaxHeight() const;             // default 40, clamped to [1, 200]
        void ImageMaxHeight(std::int32_t value);
        bool OpenPreviewAutomatically() const;           // default true
        void OpenPreviewAutomatically(bool value);
        std::int32_t PreviewDelay() const;               // ms, default 1500, clamped to [200, 100000]
        void PreviewDelay(std::int32_t value);
        Kleio::HighlightMatch HighlightMatch() const;    // default Bold
        void HighlightMatch(Kleio::HighlightMatch value);
        bool ShowSpecialSymbols() const;                 // default true
        void ShowSpecialSymbols(bool value);
        bool ShowTrayIcon() const;                       // default true
        void ShowTrayIcon(bool value);
        TrayIconStyle TrayIcon() const;                  // default Kleio
        void TrayIcon(TrayIconStyle value);
        bool ShowRecentCopyInTray() const;               // default false (shown in the tray tooltip)
        void ShowRecentCopyInTray(bool value);
        bool ShowSearch() const;                         // default true
        void ShowSearch(bool value);
        Kleio::SearchVisibility SearchVisibility() const;// default Always
        void SearchVisibility(Kleio::SearchVisibility value);
        bool ShowTitle() const;                          // default true ("Kleio" title before the search box)
        void ShowTitle(bool value);
        bool ShowApplicationIcons() const;               // default false
        void ShowApplicationIcons(bool value);
        bool ShowHexColorSwatch() const;                 // default true
        void ShowHexColorSwatch(bool value);
        bool ShowFooter() const;                         // default true
        void ShowFooter(bool value);
        double WindowWidth() const;                      // popup size in DIPs, default 450
        void WindowWidth(double value);
        double WindowHeight() const;                     // default 800 (popup shrinks to content)
        void WindowHeight(double value);
        // Last popup position relative to the work area (0..1), anchor = top middle. default (0.5, 0.2)
        // NOTE: unlike macOS, y grows downwards.
        double WindowPositionX() const;
        void WindowPositionX(double value);
        double WindowPositionY() const;
        void WindowPositionY(double value);
        void ResetWindowPosition();
        double PreviewWidth() const;                     // default 400
        void PreviewWidth(double value);

        // ---- Ignore ----
        // Executable file names (e.g. "KeePassXC.exe") or full paths; matched case-insensitively
        // against the clipboard owner's / foreground app's executable.
        std::vector<std::wstring> IgnoredApps() const;   // default empty
        void IgnoredApps(std::vector<std::wstring> const& value);
        bool IgnoreAllAppsExceptListed() const;          // default false
        void IgnoreAllAppsExceptListed(bool value);
        // Registered clipboard format names whose presence makes Kleio skip a copy.
        std::vector<std::wstring> IgnoredFormats() const;// default { "Kleio.Transient" }
        void IgnoredFormats(std::vector<std::wstring> const& value);
        std::vector<std::wstring> IgnoreRegexps() const; // ECMAScript regexes matched against copied text. default empty
        void IgnoreRegexps(std::vector<std::wstring> const& value);

        // ---- Advanced ----
        bool IgnoreEvents() const;                       // "Pause Kleio". default false
        void IgnoreEvents(bool value);
        bool IgnoreOnlyNextEvent() const;                // default false
        void IgnoreOnlyNextEvent(bool value);
        bool ClearOnQuit() const;                        // default false
        void ClearOnQuit(bool value);
        bool ClearSystemClipboard() const;               // default false
        void ClearSystemClipboard(bool value);

        // ---- Internal state ----
        bool SuppressClearAlert() const;                 // default false
        void SuppressClearAlert(bool value);
        std::vector<std::wstring> PinOrder() const;      // pin letters, in user-defined order
        void PinOrder(std::vector<std::wstring> const& value);

    private:
        Settings() = default;

        SettingsStore& Store() const;
        // Raw (serialized) value: in-memory cache first, then the store.
        std::optional<std::wstring> ReadRaw(std::wstring_view key) const;
        // Persists `value` and raises Changed(key) unless it equals `current`.
        void WriteRaw(std::wstring_view key, std::wstring const& value, std::wstring const& current);
        void RemoveRaw(std::wstring_view key);

        bool GetBool(std::wstring_view key, bool fallback) const;
        void SetBool(std::wstring_view key, bool value, bool fallback);
        std::int32_t GetInt(std::wstring_view key, std::int32_t fallback, std::int32_t minimum, std::int32_t maximum) const;
        void SetInt(std::wstring_view key, std::int32_t value, std::int32_t fallback, std::int32_t minimum, std::int32_t maximum);
        // Enum stored as int; values outside [0, maximum] read as `fallback`.
        std::int32_t GetEnum(std::wstring_view key, std::int32_t fallback, std::int32_t maximum) const;
        double GetDouble(std::wstring_view key, double fallback) const;
        void SetDouble(std::wstring_view key, double value, double fallback);
        KeyShortcut GetShortcut(std::wstring_view key, KeyShortcut fallback) const;
        void SetShortcut(std::wstring_view key, KeyShortcut value, KeyShortcut fallback);
        std::vector<std::wstring> GetList(std::wstring_view key, std::vector<std::wstring> const& fallback) const;
        void SetList(std::wstring_view key, std::vector<std::wstring> const& value, std::vector<std::wstring> const& fallback);

        mutable std::unique_ptr<SettingsStore> m_store;   // created lazily (CreateDefaultSettingsStore)
        // Cache of raw values; nullopt caches "not stored".
        mutable std::vector<std::pair<std::wstring, std::optional<std::wstring>>> m_cache;
    };
}
