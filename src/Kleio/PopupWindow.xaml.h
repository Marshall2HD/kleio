#pragma once

#include "PopupWindow.g.h"

#include "Core/Event.h"
#include "Core/HistoryItem.h"
#include "Core/KeyShortcut.h"
#include "Core/Settings.h"

#include <winrt/Kleio.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace winrt::Kleio::implementation
{
    struct HistoryItemViewModel;
    struct HistoryItemRow;

    // Port of Maccy's floating panel: ContentView + AppState/NavigationManager (selection, keyboard),
    // SlideoutController (preview), Popup (sizing) and FloatingPanel (window behaviour).
    struct PopupWindow : PopupWindowT<PopupWindow>
    {
        PopupWindow() = default;
        ~PopupWindow();

        void InitializeComponent();

        // ---- IDL ----
        void ShowPopup();
        void HidePopup();
        bool IsShown() const noexcept { return m_shown; }
        bool WasAutoHiddenWithin(uint32_t milliseconds) const noexcept;

        // ---- popup shortcut (Maccy Popup: PopupState toggle / opening / cycle) ----
        // Call right after ShowPopup() when the global popup shortcut opened the popup: the popup
        // enters the "opening" state until every modifier key is released.
        void OpenedByShortcut();
        // The global popup shortcut was pressed again while the popup is shown. In the opening or
        // cycle state this highlights the next item (cycle); releasing the modifiers then selects it
        // and closes the popup. Returns false in the toggle state: the caller closes the popup.
        // `shortcut` is the pressed shortcut (empty when unknown).
        bool HandleShortcutPressed(::Kleio::KeyShortcut const& shortcut);

    private:
        enum class ShortcutState
        {
            Toggle,    // default; the shortcut toggles the popup
            Opening,   // opened by the shortcut, its modifiers still held: toggle or cycle?
            Cycle,     // the shortcut was pressed again: each press highlights the next item
        };
        using ViewModel = winrt::Kleio::HistoryItemViewModel;
        using SourceVector = winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable>;

        // Where and on which monitor the popup opens.
        struct Target
        {
            ::Kleio::PopupPosition mode = ::Kleio::PopupPosition::Cursor;
            HMONITOR monitor = nullptr;
            RECT work{};
            double scale = 1.0;
            POINT cursor{};
            std::optional<RECT> anchor;   // tray icon / active window rectangle
        };

        // ---- setup ----
        void ConfigureWindow();
        void EnsureInitialized();
        void SetupFooter();
        void SetupPreviewToolbar();
        void SetupSplitter();
        static LRESULT CALLBACK SubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR data);

        // ---- events ----
        void HandleActivated(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::WindowActivatedEventArgs const& args);
        void HandlePreviewKeyDown(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args);
        void HandlePreviewKeyUp(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args);
        void HandleHistoryChanged();
        void HandleSettingChanged(std::wstring_view key);
        void HandlePasteStackChanged();
        void HandleItemClick(winrt::Windows::Foundation::IInspectable const& clicked);
        void HandleRowPointerMoved(HistoryItemRow& row, winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void HandleRowRightTapped(HistoryItemRow& row, winrt::Microsoft::UI::Xaml::Input::RightTappedRoutedEventArgs const& args);
        void HandleFooterPointerMoved(int index, winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void HandlePinnedDragCompleted(winrt::Microsoft::UI::Xaml::Controls::DragItemsCompletedEventArgs const& args);
        void HandleSizeMoveEnded();
        bool PointerReallyMoved(winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);

        // ---- keyboard (Maccy KeyHandlingView / KeyChord) ----
        bool HandleKey(uint32_t key, ::Kleio::Modifiers modifiers);
        void UpdateModifiers();
        void SetModifiers(::Kleio::Modifiers modifiers);
        void UpdateClearAll();
        ViewModel PressedShortcutItem(uint32_t key, ::Kleio::Modifiers modifiers) const;
        void SetSearchQuery(std::wstring const& text);
        void CheckShortcutModifiers();
        void ResetShortcutState();

        // ---- settings / chrome ----
        void ApplySettings();
        bool UpdateHeaderVisibility();
        void UpdateChrome();
        void UpdateThumbnailWidth();
        void UpdatePreviewToggleTooltip();

        // ---- data ----
        void Refresh(bool historyChanged);
        void RefreshPreservingSelection();
        ViewModel Acquire(::Kleio::HistoryItemPtr const& item);
        void PruneCache();
        static void SyncSource(SourceVector const& source, std::vector<HistoryItemViewModel*>& mirror, std::vector<ViewModel> const& desired);
        void RebuildPinnedMirror();
        void UpdatePasteStack();
        void UpdatePasteStackSelection();
        bool PasteStackVisible() const;

        // ---- search ----
        void HandleSearchTextChanged();
        void ApplyQuery();
        void SetQueryNow(std::wstring const& text);
        void FocusSearch();
        bool IsSearchFocused();

        // ---- selection (Maccy NavigationManager) ----
        bool IsMultiSelect() const noexcept;
        bool PasteStackSelected() const;
        int VisibleIndex(ViewModel const& viewModel) const;
        ViewModel FindVisible(std::int64_t id) const;
        std::vector<int> FooterItems() const;
        HistoryItemRow* FooterRow(int index);
        void SetSelection(std::vector<ViewModel> selection, ViewModel const& lead, int footer, bool scroll);
        void SelectItem(ViewModel const& viewModel, bool scroll);
        void SelectFooter(int index);
        void SelectNothing();
        void SelectInitial();
        void SelectFirstUnpinned();
        void ScrollIntoView(ViewModel const& viewModel);
        void UpdateFooterSelection();
        void KeyboardFirst();
        void KeyboardLast();
        void KeyboardNext(bool allowCycle = false);
        void KeyboardPrevious();
        void ExtendNext(bool forward);
        void ExtendRange(bool toLast);
        void ToggleInSelection(ViewModel const& viewModel);
        void Announce(ViewModel const& viewModel);

        // ---- actions ----
        void ActivateSelection(::Kleio::Modifiers modifiers);
        void SelectWithModifiers(::Kleio::HistoryItemPtr const& item, ::Kleio::Modifiers modifiers);
        void ActivateFooter(int index);
        void DeleteSelection();
        void TogglePinSelection();
        void StopPasteStack();
        void CopyRecognizedText();
        void OpenContextMenuForLead();
        void ShowItemMenu(ViewModel const& viewModel, winrt::Microsoft::UI::Xaml::UIElement const& target, winrt::Windows::Foundation::Point const& position);
        void AfterTransientUi();
        void RequestHide();
        winrt::fire_and_forget RequestClear(bool all);
        winrt::fire_and_forget OpenEditor(ViewModel viewModel);
        void PrepareDialog(winrt::Microsoft::UI::Xaml::Controls::ContentDialog const& dialog);
        void BeginDialog(double minWidth, double minHeight);
        void EndDialog();

        // ---- preview (Maccy SlideoutController / PreviewItemView) ----
        void TogglePreview(bool manual);
        void ApplyPreviewLayout();
        void UpdatePreview();
        void StartAutoOpen();
        void HandleAutoOpenTick();

        // ---- geometry (Maccy Popup / FloatingPanel / PopupPosition) ----
        Target ChooseTarget() const;
        POINT ComputeOrigin(Target const& target, int width, int height);
        void PlaceAndShow(Target const& target);
        void BringToFront();
        double HeaderHeight() const noexcept;
        bool FooterVisible() const noexcept { return m_footerVisible; }
        double ContentHeight();
        double DesiredHeight(double workHeight);
        void FitHeight();
        bool GetClientScreenRect(RECT& rect) const;
        void SetClientRect(int x, int y, int width, int height);
        void SaveLastPosition();
        void SetScale(double scale);

        // ---- state ----
        HWND m_hwnd = nullptr;
        bool m_initialized = false;
        bool m_shown = false;
        bool m_settingsDirty = true;
        std::uint64_t m_autoHiddenAt = 0;

        SourceVector m_pinnedSource{ nullptr };
        SourceVector m_unpinnedSource{ nullptr };
        std::vector<HistoryItemViewModel*> m_pinnedMirror;
        std::vector<HistoryItemViewModel*> m_unpinnedMirror;
        std::unordered_map<std::int64_t, ViewModel> m_cache;
        std::vector<ViewModel> m_pinned;
        std::vector<ViewModel> m_unpinned;
        std::vector<ViewModel> m_visible;

        std::vector<ViewModel> m_selection;
        ViewModel m_lead{ nullptr };
        int m_footerSelection = -1;
        bool m_manualMulti = false;
        std::optional<std::int64_t> m_pendingSelectId;
        int m_batchDepth = 0;
        bool m_refreshPending = false;

        std::wstring m_query;
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_searchTimer{ nullptr };
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_previewTimer{ nullptr };

        bool m_pinTop = true;
        bool m_footerVisible = true;
        bool m_headerVisible = true;
        bool m_showClearAll = false;

        ShortcutState m_shortcutState = ShortcutState::Toggle;
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_modifierTimer{ nullptr };

        bool m_keyboardNavigating = true;
        bool m_hasPointerPosition = false;
        winrt::Windows::Foundation::Point m_pointerPosition{};

        bool m_dialogOpen = false;
        bool m_flyoutOpen = false;
        bool m_dragInProgress = false;
        bool m_inSizeMove = false;
        std::optional<RECT> m_rectBeforeDialog;

        bool m_previewOpen = false;
        bool m_placementLeft = false;
        bool m_autoOpenSuppressed = false;
        std::int64_t m_previewItemId = 0;
        std::int64_t m_previewImageId = 0;
        winrt::Microsoft::UI::Xaml::Media::Imaging::BitmapImage m_previewBitmap{ nullptr };

        bool m_splitterDragging = false;
        double m_splitterStartX = 0;
        double m_splitterStartWidth = 0;

        double m_scale = 1.0;
        bool m_anchorBottom = false;
        int m_lastClientWidth = 0;
        int m_lastClientHeight = 0;

        ::Kleio::Event<>::Token m_historyToken = 0;
        ::Kleio::Event<>::Token m_pasteStackToken = 0;
        ::Kleio::Event<std::wstring_view>::Token m_settingsToken = 0;
    };
}

namespace winrt::Kleio::factory_implementation
{
    struct PopupWindow : PopupWindowT<PopupWindow, implementation::PopupWindow>
    {
    };
}
