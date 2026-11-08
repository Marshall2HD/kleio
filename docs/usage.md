# Usage

## Opening Kleio

Press <kbd>Win</kbd> + <kbd>Shift</kbd> + <kbd>C</kbd> to open Kleio, or click its icon in the notification area.

You can also make <kbd>Win</kbd> + <kbd>V</kbd> open Kleio; see [Windows clipboard integration](#windows-clipboard-integration).

## Basics

1. Type what you want to find.
2. To copy the history item you want, press <kbd>Enter</kbd>, click the item, or use the <kbd>Ctrl</kbd> + `n` shortcut.
3. To copy the history item and paste it, press <kbd>Alt</kbd> + <kbd>Enter</kbd>, <kbd>Alt</kbd> + click the item, or use <kbd>Alt</kbd> + `n`.
4. To copy the history item and paste it without formatting, press <kbd>Alt</kbd> + <kbd>Shift</kbd> + <kbd>Enter</kbd>, <kbd>Alt</kbd> + <kbd>Shift</kbd> + click the item, or use <kbd>Alt</kbd> + <kbd>Shift</kbd> + `n`.
5. To delete the history item, press <kbd>Alt</kbd> + <kbd>Delete</kbd>.
6. To see the full text of the history item, open the preview with <kbd>Ctrl</kbd> + <kbd>Space</kbd>, or wait for it to open automatically.
7. To pin the history item so that it stays at the top of the list, press <kbd>Alt</kbd> + <kbd>P</kbd>.
   The item moves to the top and gets a random but permanent shortcut letter. Press <kbd>Alt</kbd> + <kbd>P</kbd> again to unpin it.
   Drag pinned items to reorder them.
8. To select several items, hold <kbd>Shift</kbd> with the arrow keys. Pressing <kbd>Enter</kbd> starts a *paste stack*:
   every time you paste with <kbd>Ctrl</kbd> + <kbd>V</kbd>, the next item is put on the clipboard.
9. To clear all unpinned items, choose *Clear* in the footer or press <kbd>Ctrl</kbd> + <kbd>Alt</kbd> + <kbd>Backspace</kbd>.
   To clear everything including pins, hold <kbd>Shift</kbd> (*Clear all*) or press <kbd>Ctrl</kbd> + <kbd>Alt</kbd> + <kbd>Shift</kbd> + <kbd>Backspace</kbd>.
10. Right-click an item for Copy, Paste, Paste without formatting, Pin, Edit and Delete.
11. To pause Kleio and ignore new copies, <kbd>Alt</kbd> + click the notification-area icon or choose *Pause Kleio* from its menu.
12. To ignore only the next copy, <kbd>Alt</kbd> + <kbd>Shift</kbd> + click the icon or choose *Ignore next copy*.
13. To customise the behaviour, open *Settings* (<kbd>Ctrl</kbd> + <kbd>,</kbd>).
    *Settings → General → Notifications and sounds* turns on a notification and a sound for each copy.

The pin, delete, preview and open shortcuts can be changed in *Settings → General*. The copy and paste
modifiers swap when *Paste automatically* or *Paste without formatting* is enabled, the same as in Maccy.

## Windows clipboard integration

Windows has its own clipboard history on <kbd>Win</kbd> + <kbd>V</kbd>. Enable **Replace Win+V** in
*Settings → General* and, while Kleio runs, <kbd>Win</kbd> + <kbd>V</kbd> opens Kleio instead. Kleio
doesn't change any Windows settings: turn the option off, or quit Kleio, and Windows behaves as before.

<kbd>Win</kbd> + <kbd>C</kbd> belongs to Windows' Copilot key setting, which can't be turned off (it opens
Settings when no Copilot app is installed). Enable **Block Win+C** in *Settings → General* and, while
Kleio runs, <kbd>Win</kbd> + <kbd>C</kbd> does nothing.

## Command line

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

### Pause and control from the command line

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

## Ignore rules

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
