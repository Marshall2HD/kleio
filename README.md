<img width="128px" src="docs/logo.png" alt="Kleio logo" />

# Kleio

Kleio is a lightweight clipboard manager for Windows 11. It keeps the history of what you copy
and lets you quickly navigate, search, and use previous clipboard contents.

Kleio is a port of [Maccy](https://github.com/p0deje/Maccy) to Windows. It keeps Maccy's layout,
behaviour and keyboard-first workflow. The only deliberate changes adopt Windows conventions:
Fluent design, a notification-area icon instead of a menu bar item, and Windows key bindings.

Built with WinUI 3 (Windows App SDK 2.5), C++/WinRT and MSIX packaging.

* [Features](#features)
* [Install](#install)
* [Usage](#usage)
* [Windows clipboard integration](#windows-clipboard-integration)
* [Advanced](#advanced)
* [Build from source](#build-from-source)
* [FAQ](#faq)
* [License](#license)

## Features

* Lightweight and fast: native C++, event-driven clipboard monitoring, SQLite storage
* Keyboard-first
* Secure and private: history never leaves your PC; honours the Windows "don't record this" clipboard formats used by password managers
* Native Windows 11 UI: Acrylic popup, Mica settings, light/dark theme, Segoe Fluent icons
* Search: exact, fuzzy, regular expression, or mixed
* Pins, preview pane, paste stack, item editor, OCR titles for images
* Open source and free (MIT)

## Install

Download the `.msix` for your architecture (x64 or ARM64) from the
[releases](https://github.com/Marshall2HD/kleio/releases/latest) page and open it.

Development builds from CI are signed with a throw-away test certificate. Import the bundled
`Kleio.cer` into *Local Machine → Trusted People* first, then install the package:

```powershell
Import-Certificate -FilePath .\Kleio.cer -CertStoreLocation Cert:\LocalMachine\TrustedPeople   # elevated
Add-AppxPackage .\Kleio_0.1.0.0_x64.msix
```

Windows 11 (build 22000) or later is required.

## Usage

1. <kbd>Win</kbd> + <kbd>Alt</kbd> + <kbd>V</kbd> to open Kleio, or click its icon in the notification area.
   You can also make <kbd>Win</kbd> + <kbd>V</kbd> open Kleio; see [Windows clipboard integration](#windows-clipboard-integration).
2. Type what you want to find.
3. To copy the history item you want, press <kbd>Enter</kbd>, click the item, or use the <kbd>Ctrl</kbd> + `n` shortcut.
4. To copy the history item and paste it, press <kbd>Alt</kbd> + <kbd>Enter</kbd>, <kbd>Alt</kbd> + click the item, or use <kbd>Alt</kbd> + `n`.
5. To copy the history item and paste it without formatting, press <kbd>Alt</kbd> + <kbd>Shift</kbd> + <kbd>Enter</kbd>, <kbd>Alt</kbd> + <kbd>Shift</kbd> + click the item, or use <kbd>Alt</kbd> + <kbd>Shift</kbd> + `n`.
6. To delete the history item, press <kbd>Alt</kbd> + <kbd>Delete</kbd>.
7. To see the full text of the history item, open the preview with <kbd>Ctrl</kbd> + <kbd>Space</kbd>, or wait for it to open automatically.
8. To pin the history item so that it stays at the top of the list, press <kbd>Alt</kbd> + <kbd>P</kbd>.
   The item moves to the top and gets a random but permanent shortcut letter. Press <kbd>Alt</kbd> + <kbd>P</kbd> again to unpin it.
   Drag pinned items to reorder them.
9. To select several items, hold <kbd>Shift</kbd> with the arrow keys. Pressing <kbd>Enter</kbd> starts a *paste stack*:
   every time you paste with <kbd>Ctrl</kbd> + <kbd>V</kbd>, the next item is put on the clipboard.
10. To clear all unpinned items, choose *Clear* in the footer or press <kbd>Ctrl</kbd> + <kbd>Alt</kbd> + <kbd>Backspace</kbd>.
    To clear everything including pins, hold <kbd>Shift</kbd> (*Clear all*) or press <kbd>Ctrl</kbd> + <kbd>Alt</kbd> + <kbd>Shift</kbd> + <kbd>Backspace</kbd>.
11. Right-click an item for Copy, Paste, Paste without formatting, Pin, Edit and Delete.
12. To pause Kleio and ignore new copies, <kbd>Alt</kbd> + click the notification-area icon or choose *Pause Kleio* from its menu.
13. To ignore only the next copy, <kbd>Alt</kbd> + <kbd>Shift</kbd> + click the icon or choose *Ignore next copy*.
14. To customise the behaviour, open *Settings* (<kbd>Ctrl</kbd> + <kbd>,</kbd>).

The pin, delete, preview and open shortcuts can be changed in *Settings → General*. The copy and paste
modifiers swap when *Paste automatically* or *Paste without formatting* is enabled, the same as in Maccy.

## Windows clipboard integration

Windows has its own clipboard history on <kbd>Win</kbd> + <kbd>V</kbd>. Enable **Replace Win+V** in
*Settings → General* and, while Kleio runs, <kbd>Win</kbd> + <kbd>V</kbd> opens Kleio instead. Kleio
doesn't change any Windows settings: turn the option off, or quit Kleio, and Windows behaves as before.

## Advanced

### Ignore copied items

Pause Kleio from the command line, for example around a script that handles secrets:

```powershell
kleio --pause        # ignore all copies
kleio --resume       # record again
kleio --ignore-next  # ignore only the next copy
kleio --settings     # open Settings
kleio --quit
```

### Ignore applications, formats and patterns

*Settings → Ignore* lets you skip copies:

* **Applications**: copies from the listed apps (or from every app except the listed ones).
* **Clipboard formats**: copies that contain one of the listed clipboard formats. Kleio always
  ignores `ExcludeClipboardContentFromMonitorProcessing` and `Clipboard Viewer Ignore`. It also
  ignores content that is marked as excluded from clipboard history (`CanIncludeInClipboardHistory` = 0).
  Password managers such as KeePassXC, Bitwarden and 1Password set these formats.
  Use a clipboard viewer such as [InsideClipboard](https://www.nirsoft.net/utils/inside_clipboard.html) to find the format names an app uses.
* **Regular expressions**: text copies that match one of the patterns (for example `^[0-9]{6}$` for one-time codes).

## Build from source

Requirements: Visual Studio 2026 (or 2022 17.14+) with the *Desktop development with C++* and
*WinUI application development* workloads, and the Windows 11 SDK.

```powershell
msbuild Kleio.sln /t:Restore /p:Configuration=Release /p:Platform=x64
msbuild Kleio.sln /p:Configuration=Release /p:Platform=x64
```

Or open `Kleio.sln`, set the platform to x64 or ARM64 and press <kbd>F5</kbd>. Core logic has a test runner in
`tests/Kleio.Core.Tests`. See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for how the code is organised.

## FAQ

### Why doesn't it paste when I select an item in history?

1. Make sure *Paste automatically* is enabled in *Settings → General*, or use <kbd>Alt</kbd> + <kbd>Enter</kbd>.
2. Windows does not let normal apps send keystrokes to apps running as administrator. If the target
   app is elevated, the item is still copied, so press <kbd>Ctrl</kbd> + <kbd>V</kbd> yourself.

### The shortcut to open Kleio doesn't work

Another app has probably registered the same shortcut. Kleio shows a warning in *Settings → General*.
Pick another shortcut, or enable *Replace Win+V*.

### How do I restore the hidden footer?

Open Kleio and press <kbd>Ctrl</kbd> + <kbd>,</kbd>, or choose *Settings…* from the notification-area
icon's menu, then enable *Show footer* in *Appearance*.

### Where is my history stored?

In the app's local data folder (`%LOCALAPPDATA%\Packages\Kleio.ClipboardManager_*\LocalState\history.sqlite`).
Uninstalling Kleio removes it.

## License

[MIT](LICENSE). Kleio is a port of [Maccy](https://github.com/p0deje/Maccy) by Alex Rodionov, also
MIT-licensed. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
