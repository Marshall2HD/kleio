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
* [Translations](#translations)
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
* Scriptable from the command line: get, select, delete and clear history items
* Updates itself from GitHub releases
* In 43 languages, the same as Maccy
* Open source and free (MIT)

## Install

Download the `.msix` for your architecture (x64 or ARM64) from the
[releases](https://github.com/Marshall2HD/kleio/releases/latest) page and open it.

Development builds from CI (the `Kleio-x64` / `Kleio-ARM64` artifacts of the *Build* workflow) are
signed with a throw-away test certificate and ship the runtime packages they depend on (Windows App
SDK runtime and the VC++ runtime for desktop apps).
Trust the bundled `Kleio.cer` first (once, from an elevated PowerShell), then install:

```powershell
Import-Certificate -FilePath .\Kleio.cer -CertStoreLocation Cert:\LocalMachine\TrustedPeople
$packages = Get-ChildItem -Recurse -Include *.msix, *.appx
$packages | Where-Object DirectoryName -like '*Dependencies*' | ForEach-Object { Add-AppxPackage $_.FullName }
$packages | Where-Object Name -like 'Kleio_*' | ForEach-Object { Add-AppxPackage $_.FullName }
```

Windows 11 (build 22000) or later is required.

## Usage

1. <kbd>Win</kbd> + <kbd>Shift</kbd> + <kbd>C</kbd> to open Kleio, or click its icon in the notification area.
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
    *Settings → General → Notifications and sounds* turns on a notification and a sound for each copy.

The pin, delete, preview and open shortcuts can be changed in *Settings → General*. The copy and paste
modifiers swap when *Paste automatically* or *Paste without formatting* is enabled, the same as in Maccy.

## Windows clipboard integration

Windows has its own clipboard history on <kbd>Win</kbd> + <kbd>V</kbd>. Enable **Replace Win+V** in
*Settings → General* and, while Kleio runs, <kbd>Win</kbd> + <kbd>V</kbd> opens Kleio instead. Kleio
doesn't change any Windows settings: turn the option off, or quit Kleio, and Windows behaves as before.

## Advanced

### Script Kleio from the command line

Kleio's counterpart to Maccy's Shortcuts actions. The `kleio` command works from PowerShell, the
Command Prompt and scripts; it talks to the running Kleio and starts it in the background if needed.

```powershell
kleio --get                  # the item selected in Kleio (else the first unpinned item)
kleio --get 3                # the 3rd item
kleio --get 1 --format html  # text, html, rtf, image or files
kleio --get 2 --format image --output shot.png
kleio --select 2             # copy the 2nd item, as if you chose it in Kleio
kleio --delete 4             # delete the 4th item
kleio --clear                # clear the history except pinned items (asks first)
kleio --clear --yes          # ... without asking
kleio --help
```

* Items are numbered from 1 in the order Kleio lists them, pinned items included.
* `--get` prints the item's text. Without `--format` it prints the first of text, files (one path
  per line), image, HTML and RTF that the item has. Images are printed as base64; use `--output`
  to save the PNG instead. `--output` writes any format to a file and prints the file's path.
* `--select` copies the item and also pastes it when *Paste automatically* is on, then prints the item's title.
* `--clear` asks for confirmation unless you chose *Don't ask again* when clearing in Kleio.
* Output is UTF-8. In Windows PowerShell 5.1, set `[Console]::OutputEncoding = [Text.Encoding]::UTF8`
  before capturing non-ASCII text, or use `--output`.
* Exit codes: `0` success, `1` no such item (or it lacks the requested format), `2` invalid
  arguments, `3` Kleio couldn't be reached, `4` you cancelled the confirmation. Errors go to stderr.
* `kleio.exe` is a windowed app, so an interactive shell may show its prompt before the output
  appears. Scripts, pipes and redirection (`$text = kleio --get 1`, `kleio --get 1 > item.txt`)
  wait for it as usual.

### Ignore copied items

Pause Kleio from the command line, for example around a script that handles secrets:

```powershell
kleio --pause        # ignore all copies
kleio --resume       # record again
kleio --ignore-next  # ignore only the next copy
kleio --settings     # open Settings
kleio --quit
```

You can also <kbd>Alt</kbd> + click the notification-area icon. To ignore only the next copy,
<kbd>Alt</kbd> + <kbd>Shift</kbd> + click it. The icon is dimmed while Kleio is paused, or when
*Settings → Storage* saves no type at all.

### Ignore applications, formats and patterns

*Settings → Ignore* lets you skip copies:

* **Applications**: copies from the listed apps (or from every app except the listed ones).
* **Clipboard formats**: copies that contain one of the listed clipboard formats. Kleio always
  ignores these, which password managers such as KeePassXC, Bitwarden and 1Password set:
  * `ExcludeClipboardContentFromMonitorProcessing`
  * `Clipboard Viewer Ignore`
  * `CanIncludeInClipboardHistory` set to 0 (content excluded from clipboard history)

  The list also contains `Kleio.Transient` by default; you can remove it or add your own formats.
  Use a clipboard viewer such as [InsideClipboard](https://www.nirsoft.net/utils/inside_clipboard.html) to find the format names an app uses.
* **Regular expressions**: text copies that match one of the patterns (for example `^[0-9]{6}$` for one-time codes).

## Translations

Kleio speaks the 43 languages Maccy is translated into: Arabic, Belarusian, Bengali, Bosnian,
Catalan, Central Kurdish, Chinese (Simplified and Traditional), Croatian, Czech, Dutch, English,
Esperanto, Finnish, French, German, Greek, Hebrew, Hindi, Hungarian, Indonesian, Italian, Japanese,
Khmer, Korean, Latvian, Lithuanian, Norwegian Bokmål, Persian, Polish, Portuguese (Brazil and
Portugal), Romanian, Russian, Slovenian, Spanish, Swedish, Tamil, Thai, Turkish, Ukrainian, Uzbek
and Vietnamese. Right-to-left languages are laid out right to left.

By default Kleio follows the Windows display language. *Settings → General → Language* picks
another one for Kleio only (restart Kleio to apply it), like a per-app language on macOS.
Shortcuts are shown with the key names printed on keyboards in that language (*Strg+Umschalt+C*
in German); settings and the command line always use the English names.

The strings live in `src/Kleio/Strings/<language>/Resources.resw`, one file per language; `en-US`
is the source. Wherever Maccy has the same text, the translation comes from
[Maccy's translations on Weblate](https://hosted.weblate.org/projects/maccy/), adapted to Windows
(*Ctrl* instead of ⌘, *notification area* instead of menu bar, *Settings* instead of Preferences).
The Windows-specific texts and the gaps in Maccy's translations were translated for Kleio and
deserve a review by native speakers.

To improve a translation, edit the `<value>` of a key in the language's `Resources.resw` (any text
editor, or the resource editor of Visual Studio) and open a pull request. Keep `{0}`-style
placeholders and terms such as `--get` or `Kleio.Transient` as they are, and use the key names of
your keyboard in shortcuts. To add a language, copy `en-US` to a new folder named with the
BCP-47 tag Windows uses (for example `sk` or `sr-Latn`), translate it and add the language to the
list in `src/Kleio/Strings.cpp`. Then run:

```powershell
python tools/check_translations.py
```

It checks that every file parses, has exactly the keys of `en-US` with the same placeholders, and
that the code and XAML only use keys that exist. CI runs it too.

The command-line help (`kleio --help`) stays in English; its error messages are translated.

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

### How does Kleio update?

With *Check for updates automatically* on (*Settings → General*), Kleio looks for a new
[release](https://github.com/Marshall2HD/kleio/releases) once a day. When there is one, a
notification offers to install it or skip that version. *Check now* (also in *About*) checks
right away. Installing downloads the `.msix` for your PC and updates Kleio, which then restarts.
If that fails, Kleio offers to open the package in App Installer or the release page.

### Where is my history stored?

In the app's local data folder (`%LOCALAPPDATA%\Packages\Kleio.ClipboardManager_*\LocalState\history.sqlite`).
Uninstalling Kleio removes it.

## License

[MIT](LICENSE). Kleio is a port of [Maccy](https://github.com/p0deje/Maccy) by Alex Rodionov, also
MIT-licensed. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
