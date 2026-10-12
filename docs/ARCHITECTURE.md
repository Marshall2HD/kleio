# Kleio architecture

Kleio is a port of [Maccy](https://github.com/p0deje/Maccy) (macOS clipboard manager) to Windows 11:
WinUI 3 (Windows App SDK 2.5), C++20 with C++/WinRT 3.0, packaged as MSIX. Layout and features follow
Maccy; look and feel follow the Windows 11 / Fluent design guidelines.

## Layout

```
Kleio.sln
Directory.Build.props / .targets / Directory.Packages.props   central build settings + NuGet versions
src/Kleio/                     the app (single-project MSIX)
  App.xaml(.h/.cpp)            entry point, wires AppController
  AppController.h/.cpp         composition root + app-level actions (Maccy's AppState/AppDelegate)
  *.idl, *.xaml, *.xaml.h/.cpp WinRT classes: windows, pages, controls, view models (MUST stay in root)
  Themes/*.xaml                resource dictionaries merged by App.xaml
  Core/                        platform-neutral logic: settings, model, storage, search, sorting
  Platform/                    Win32 integration: clipboard, hot keys, hook, tray, paste, OCR, ...
  Assets/                      logos, Kleio.ico, Sounds/*.wav
  Strings/en-US/*.resw         (future) localized strings
tests/Kleio.Core.Tests/        console test runner for Core (built and run in CI)
tools/generate_assets.py       regenerates the logo PNGs/ICO
```

The vcxproj picks files up by glob, so adding a file to the right folder is enough:
`*.h/*.cpp` in root, `Core/`, `Platform/`; `*.idl` and `*.xaml` in root; `Themes/*.xaml`.
Exception: a runtime class *without* XAML that is used in markup (e.g. `SettingsCard`) needs an
explicit `ClInclude`/`ClCompile` entry with `<DependentUpon>Foo.idl</DependentUpon>` in the vcxproj,
or the XAML compiler can't find its implementation header for `XamlTypeInfo.g.cpp`.

## Conventions

* C++20, `/W4`, `NOMINMAX`, `WIN32_LEAN_AND_MEAN`. Every `.cpp` starts with `#include "pch.h"`.
* Plain C++ lives in `namespace Kleio`; WinRT implementation types in `winrt::Kleio::implementation`.
* Include project headers relative to `src/Kleio` (e.g. `#include "Core/History.h"`).
* Everything runs on the UI thread (the XAML dispatcher thread, which also pumps the Win32 messages
  of the hidden `MessageWindow`). Background work (OCR, HTTP) uses coroutines and hops back with
  `co_await wil::resume_foreground(dispatcherQueue)`.
* C++/WinRT 3.0: **do not call `InitializeComponent()` from constructors**; it is invoked
  automatically after construction. Override `void InitializeComponent()` (calling the base
  `XxxT<Xxx>::InitializeComponent()` first) if you need to touch named elements at startup.
* Code-behind includes: `#include "Foo.xaml.h"` then
  `#if __has_include("Foo.g.cpp")` / `#include "Foo.g.cpp"` / `#endif`.
* Runtime classes referencing classes from another `.idl` must `import "Other.idl";`.
* Errors: Win32 failures are non-fatal; log with `OutputDebugStringW` and carry on. Never let an
  exception escape an event handler or a Win32 callback.
* Windows look: Mica for normal windows (Settings), Desktop Acrylic for the popup, `SymbolIcon` /
  `FontIcon` with *Segoe Fluent Icons*, theme resources (`TextFillColorSecondaryBrush`, `CardBackgroundFillColorDefaultBrush`, ...)
  instead of hard-coded colours, 8px corner radius (`OverlayCornerRadius`/`ControlCornerRadius`), keyboard
  shortcuts written as `Ctrl+Shift+C`.

## macOS → Windows mapping

| Maccy | Kleio |
|---|---|
| Menu bar status item | Notification-area (tray) icon; left click opens the popup, right click shows a context menu |
| `⇧⌘C` global shortcut | `Win+Shift+C` (configurable). Optional *Replace Win+V* takes over the Windows clipboard shortcut |
| `⌘` / `⌥` modifiers | `Ctrl` / `Alt` (e.g. `Ctrl+1` copy 1st item, `Alt+Enter` paste, `Alt+Shift+Enter` paste plain) |
| `⌥⌫` delete item, `⌥P` pin, `⌃Space` preview | `Alt+Delete`, `Alt+P`, `Ctrl+Space` (configurable) |
| `⌥⌘⌫` clear, `⇧⌥⌘⌫` clear all | `Ctrl+Alt+Backspace`, `Ctrl+Alt+Shift+Backspace` |
| `⌘,` preferences, `⌘Q` quit | `Ctrl+,`, `Ctrl+Q` |
| NSPasteboard polling (every 0.5 s) | `AddClipboardFormatListener`, debounced: a burst of `WM_CLIPBOARDUPDATE`s (e.g. `OleSetClipboard` + `OleFlushClipboard`) is one copy |
| All persistable pasteboard types (minus `dyn.*`, OLE sources) | All persistable clipboard formats (minus synthesized ones, GDI handles, OLE plumbing, shell drag state); see `Core/ClipboardFormats.h` |
| Pasteboard types (`org.nspasteboard.ConcealedType`...) | Clipboard formats (`ExcludeClipboardContentFromMonitorProcessing`, `CanIncludeInClipboardHistory`=0, ...) |
| Bundle identifiers | Executable paths / names |
| SwiftData | SQLite (`winsqlite3`, part of Windows) in the package's LocalFolder |
| UserDefaults | `ApplicationData.LocalSettings` |
| Vision OCR for image titles | `Windows.Media.Ocr` |
| LaunchAtLogin | MSIX `StartupTask` |
| Sparkle | GitHub Releases check |
| Accessibility permission (to paste) | not needed: `SendInput` |
| `defaults write ignoreEvents` | `kleio.exe --pause` / `--resume` / `--ignore-next` (app execution alias) |

### Windows clipboard conflicts

Kleio does not change any Windows settings. The only integration is **Replace Win+V**
(Settings → General): while Kleio runs, a low-level keyboard hook intercepts `Win+V` and opens
Kleio's popup instead of the Windows clipboard history. Turning it off (or quitting Kleio) restores
the Windows behaviour immediately. A link opens *Settings → System → Clipboard* for users who also
want to turn off the built-in history themselves.

## Component map

```
App ──► AppController ──► Settings (LocalSettings)            Core
                     ├──► Storage (SQLite) ◄── History ──► Search, Sorter
                     ├──► MessageWindow ◄── ClipboardService, HotKeyService, TrayIcon   Platform
                     ├──► KeyboardHook (Win+V, paste-stack Ctrl+V detection)
                     ├──► PasteService (SendInput Ctrl+V)
                     ├──► PopupWindow ──► HistoryItemViewModel / HistoryItemRow       UI
                     └──► SettingsWindow ──► *SettingsPage, ShortcutRecorder
```

Copy flow: `WM_CLIPBOARDUPDATE` → settle delay → `ClipboardService` filters & reads every persistable format → `NewCopy` →
`AppController::OnNewCopy` → `History::Add` (dedupe, persist, trim) → `History::Changed` → popup
refreshes; image items get an OCR title asynchronously via `History::UpdateTitle`.

Select flow: popup → `AppController::Select(item, modifiers)` → hide popup →
`ClipboardService::Copy` → (optionally) `PasteService::RestoreForegroundWindow` + `Paste`.
