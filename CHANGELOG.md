# Changelog

Changes to the Windows app of this fork (UxPlay for Windows). The receiver engine
is [UxPlay](https://github.com/FDH2/UxPlay) by its upstream authors; its own changes
are listed in the upstream README.

## [1.73.6] - 2026-10-30

First full release since **dev build 50** (2026-07-26, commit `bbb772e`). Compared
against that release: 36 files changed (+5,723 / -549), merged in pull requests
#14 to #22.

**Build details:** Windows 10 and 11, 64-bit. Installer (`uxplay-windows-installer.exe`)
and portable ZIP (`uxplay-windows-portable.zip`), built by the *Windows Build & Release*
GitHub Actions workflow with MSYS2 UCRT64 (GCC, GStreamer and FFmpeg from MSYS2).
Receiver engine: UxPlay 1.73.6.

### New Features
- **Redesigned UxPlay window.** Pages for Connection, Display, Audio and Advanced,
  a menu bar, keyboard shortcuts (F5 start, Shift+F5 stop, Ctrl+L log, F1 help),
  light and dark themes that follow Windows, and the window size, position and
  page are remembered.
- **Live connection status.** Shows *Receiver off*, *Starting*, *Disconnected*,
  *Connecting* or *Connected*, with the device's name, address, what is streaming
  and for how long.
- **Errors you can act on.** Common problems (device discovery unavailable, port
  in use, missing GStreamer plugin, invalid option, unexpected exit) are explained
  in a card with a *Try again* button.
- **Built-in console and log file.** The receiver's output appears in an in-app
  console (View → Show Console, Ctrl+\`) and in a log file that rotates at 2 MB.
- **Several phones at once.** *Connection → Phones at once* (1 to 4): while a phone
  is mirroring, another receiver ("*name* 2", ...) appears for the next one, each
  in its own window.
- **No Apple Bonjour needed.** UxPlay has its own device discovery (mDNS/DNS-SD,
  RFC 6762/6763). *Connection → Device discovery* chooses **UxPlay (built-in)**
  (default), **Automatic** (Bonjour if it is running, otherwise built-in) or
  **Apple Bonjour**.
- **Player window that fits the video.** The window takes the video's shape (no
  black bars), keeps it when resized, and is titled with the receiver's name.
  Right-click it to rotate 90° / 180°, reset the rotation or fit it to the video.
- **Choose where settings and logs are saved**, in the installer and under
  *Advanced → Storage*.
- **Copy a ready-to-run command.** *Copy* on the Advanced page (and the footer
  command) gives a line for a Command Prompt that runs exactly what Start runs,
  including the full path and the Device discovery choice. The password stays
  masked.
- **Project website** at <https://chr0mx.github.io/UxPlay/>.

### Improvements
- **No console window** opens with the receiver any more; its output goes to the
  in-app console and the log.
- **App icon** on the program files, taskbar, Alt+Tab and the notification-area
  icon. `uxplay.exe` shows as *UxPlay receiver engine* in Task Manager.
- **Lower CPU use while idle.** The window no longer redraws continuously
  (measured under Wine: about 340% of a core down to about 11%).
- **No more desktop-wide mouse lag while a receiver runs.** The right-click menu's
  mouse hook now runs on its own thread (measured under Wine: about 10 ms per
  mouse event down to about 0.5 ms).
- **Installer:** sharp text on high-DPI screens, UxPlay artwork, the version
  number in its title, and running UxPlay is closed before installing or
  uninstalling.
- **One UxPlay at a time:** starting it again brings the open window to the front.
- `uxplay.exe` can be run directly from a Command Prompt; it finds its bundled
  GStreamer by itself.

### Bug Fixes
- Closing the window could leave `uxplay.exe` running. Every receiver now ends
  with the app, even if the app is killed.
- Installer text showed "â€”" instead of "—" and was blurry on high-DPI screens.
- The installer title showed "UxPlay dev" instead of the version number.

### Changes
- Rotate and reset rotation moved from the window's buttons to the player
  window's right-click menu.
- The package's `README-Windows.txt` is now `README.md`, with a table explaining
  the files in the folder.
- Release builds take their version from `uxplay.cpp`, the same as the app.

### Removed Features
- `uxplay.bat` and the *UxPlay (command line)* Start-menu shortcut: `uxplay.exe`
  now sets up GStreamer itself. Older copies are removed when installing.
- The installer's "Apple Bonjour was not detected" message, no longer needed.

### Breaking Changes
- Scripts that called `uxplay.bat` must call `uxplay.exe` (same options).
- Device discovery now defaults to UxPlay's own responder, even when Apple
  Bonjour is installed. Choose *Automatic* or *Apple Bonjour* under
  *Connection → Device discovery* to use Bonjour.

---

## How release notes are written

Before building the release, compare all changes against the previous release. Inspect Git history, merged changes and actual code differences.

Generate professional, user-friendly release notes organised into:
- New Features
- Improvements
- Bug Fixes
- Changes
- Removed Features
- Breaking Changes

Create or update CHANGELOG.md and prepare GitHub Release notes. Include the version number, release date and build details. Only document verified changes. Do not invent features or exaggerate improvements.
