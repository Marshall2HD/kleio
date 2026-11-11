# FAQ

### Why doesn't it paste when I select an item in history?

1. Make sure *Paste automatically* is enabled in *Settings → General*, or use <kbd>Alt</kbd> + <kbd>Enter</kbd>.
2. Windows does not let normal apps send keystrokes to apps running as administrator. If the target
   app is elevated, the item is still copied, so press <kbd>Ctrl</kbd> + <kbd>V</kbd> yourself.

### The shortcut to open Locket doesn't work

Another app has probably registered the same shortcut. Locket shows a warning in *Settings → General*.
Pick another shortcut, or enable *Replace Win+V*.

### How do I restore the hidden footer?

Open Locket and press <kbd>Ctrl</kbd> + <kbd>,</kbd>, or choose *Settings…* from the notification-area
icon's menu, then enable *Show footer* in *Appearance*.

### How does Locket update?

The Microsoft Store updates Locket like any other Store app. Builds installed outside the Store
(CI or GitHub release packages) update themselves instead: with *Check for updates automatically* on
(*Settings → General*), Locket looks for a new [release](https://github.com/Marshall2HD/locket/releases)
once a day. When there is one, a
notification offers to install it or skip that version. *Check now* (also in *About*) checks
right away. Installing downloads the `.msix` for your PC and updates Locket, which then restarts.
If that fails, Locket offers to open the package in App Installer or the release page.

### Where is my history stored?

In the app's local data folder (`%LOCALAPPDATA%\Packages\MarshallHD.Locket_*\LocalState\history.sqlite`).
Uninstalling Locket removes it.
