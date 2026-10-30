# UxPlay on Windows

UxPlay runs natively on Windows 10 and 11 (64-bit) using GStreamer for audio/video rendering and its own built-in mDNS responder for AirPlay device discovery (Apple Bonjour is optional). A portable ZIP release is available that requires no installation of MSYS2, Python, or package managers.

---

## Table of Contents

1. [Quick Start (Portable ZIP)](#quick-start-portable-zip)
2. [Prerequisites](#prerequisites)
3. [Hardware Decoding](#hardware-decoding)
4. [Video Sinks](#video-sinks)
5. [Audio Sinks](#audio-sinks)
6. [Fullscreen & Multi-Monitor](#fullscreen--multi-monitor)
7. [Common Options](#common-options)
8. [Building from Source](#building-from-source)
9. [Troubleshooting](#troubleshooting)

---

## Quick Start (Portable ZIP)

1. Download the latest `uxplay-windows-<version>.zip` from the [Releases](../../../releases) page.
2. Extract to any folder (e.g. `C:\UxPlay`).
4. Double-click **`uxplay.bat`** or open a Command Prompt in the folder and run:
   ```
   uxplay.bat
   ```
5. When Windows Firewall prompts, click **Allow access** on both private and public networks.
6. On your iPhone/iPad: open **Control Center → Screen Mirroring** and tap the device name shown in UxPlay's title bar.

---

## Prerequisites

### Device discovery (no Bonjour needed)

iPhone, iPad and Mac find AirPlay receivers with mDNS/DNS-SD (RFC 6762/6763).
UxPlay for Windows has its own built-in mDNS responder, so **Apple Bonjour is not
required** — nothing extra to install, no administrator prompt.

If Apple's Bonjour Service is already installed and running (it comes with iTunes,
iCloud for Windows, the Apple Devices app or *Bonjour Print Services*), UxPlay uses it
instead. The **Device discovery** row on the Connection page shows which one is in use
("Built-in" or "Apple Bonjour"). To force the built-in responder, set the environment
variable `UXPLAY_MDNS=builtin`.

The built-in responder needs UDP port 5353 (mDNS) to be allowed by the firewall; the
installer's firewall rule for `uxplay.exe` covers it.

### Network

- The Windows PC and the iOS/iPadOS device must be on the **same Wi-Fi network** (or Ethernet + Wi-Fi bridge).
- A 5 GHz Wi-Fi network is strongly recommended for smooth 1080p mirroring.

---

## Hardware Decoding

Hardware decoding dramatically reduces CPU usage and improves latency. Pass `-vd <decoder>` to select a decoder.

### Intel / AMD — DXVA2 / D3D11VA (recommended)

Works on virtually all modern Intel and AMD integrated/discrete GPUs:

```bat
uxplay.bat -vd d3d11videodec -vs d3d11videosink
```

### NVIDIA — NVDEC

Requires an NVIDIA GPU with NVDEC support (GTX 600+ for H.264, GTX 900+ for H.265):

```bat
uxplay.bat -vd nvh264dec -vs d3d11videosink
```

For H.265/HEVC content:
```bat
uxplay.bat -vd nvh265dec -vs d3d11videosink
```

### Software Decoding (fallback)

If hardware decoding causes issues, fall back to FFmpeg software decode:

```bat
uxplay.bat -vd avdec_h264 -vs d3d11videosink
```

---

## Video Sinks

| Sink | Command | Notes |
|------|---------|-------|
| D3D11 (recommended) | `-vs d3d11videosink` | DirectX 11, works on Win10+ |
| D3D12 | `-vs d3d12videosink` | DirectX 12, supports Alt+Enter fullscreen |
| Auto (default) | `-vs autovideosink` | GStreamer picks best available |
| OpenGL | `-vs glimagesink` | Fallback for older systems |

**Note:** On some older NVIDIA drivers, `d3d12videosink` may cause a crash on first frame. Use `d3d11videosink` instead.

---

## Audio Sinks

| Sink | Command | Notes |
|------|---------|-------|
| DirectSound (default) | `-as directsoundsink` | Compatible with all Windows versions |
| WASAPI | `-as wasapisink` | Lower latency, Win Vista+ |
| WASAPI exclusive | `-as wasapi2sink` | Exclusive mode, lowest latency |
| Auto | `-as autoaudiosink` | GStreamer picks best available |

---

## Fullscreen & Multi-Monitor

### Fullscreen at startup

```bat
uxplay.bat -fs
```

### Fullscreen toggle at runtime

- With `d3d12videosink`: press **Alt+Enter** to toggle fullscreen.
- With `d3d11videosink`: use the `-fs` flag at startup; runtime toggle depends on GStreamer version.

### Specific resolution / window size

```bat
uxplay.bat -s 1920x1080
```

### Target a secondary monitor

Use the `-o` flag to select a display index (0 = primary):

```bat
uxplay.bat -vs "d3d11videosink display-index=1" -fs
```

Or set a custom GStreamer pipeline element property via `-vp`/`-vs` with quoted options.

---

## Common Options

```
uxplay.bat [options]

  -n <name>       AirPlay receiver name shown on iOS (default: hostname)
  -nh             Do not use mDNS hostname; use -n name instead
  -fs             Start in fullscreen
  -s WxH          Window size (e.g. -s 1920x1080)
  -p <port>       AirPlay TCP port (default: 7100)
  -vs <sink>      GStreamer video sink element
  -as <sink>      GStreamer audio sink element
  -vd <decoder>   GStreamer video decoder element
  -vp <parser>    GStreamer video parser element
  -t <secs>       NTP timeout in seconds (default: 5)
  -pw <password>  Require password for connections
  -pin            Require PIN authentication
  -v              Verbose / debug logging
  -h              Show help
```

---

## Building from Source

To build UxPlay from source on Windows, you need MSYS2 with the UCRT64 environment.

### 1. Install MSYS2

Download from [msys2.org](https://www.msys2.org/) and install. Open the **UCRT64** terminal.

### 2. Update and install dependencies

```bash
pacman -Syu
pacman -S mingw-w64-ucrt-x86_64-cmake \
          mingw-w64-ucrt-x86_64-ninja \
          mingw-w64-ucrt-x86_64-gcc \
          mingw-w64-ucrt-x86_64-pkg-config \
          mingw-w64-ucrt-x86_64-openssl \
          mingw-w64-ucrt-x86_64-libplist \
          mingw-w64-ucrt-x86_64-gstreamer \
          mingw-w64-ucrt-x86_64-gst-plugins-base \
          mingw-w64-ucrt-x86_64-gst-plugins-good \
          mingw-w64-ucrt-x86_64-gst-plugins-bad \
          mingw-w64-ucrt-x86_64-gst-plugins-ugly \
          mingw-w64-ucrt-x86_64-gst-libav
```

> **Note:** `dns_sd.h` is bundled in `deps/windows/` — no Bonjour SDK installer is required to build. At runtime UxPlay uses the Bonjour service if it is running, and its built-in mDNS responder otherwise (see [Prerequisites](#prerequisites)).

### 3. Build

```bash
git clone https://github.com/chr0mx/uxplay
cd uxplay
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DNO_MARCH_NATIVE=ON
cmake --build build
```

### 4. Run

```bash
./build/uxplay.exe
```

Allow Windows Firewall access when prompted.

### 5. Create portable package (optional)

```bash
bash scripts/package-windows.sh
```

This produces `uxplay-windows-<version>.zip` with all required DLLs bundled.

## GUI launcher (uxplay-gui.exe)

The **UxPlay** shortcut opens `uxplay-gui.exe`, which configures and runs `uxplay.exe` for you.

- **No console window.** The receiver runs without a console. Its output goes to the in-app
  console (**View → Show Console**, `Ctrl+``) and to a log file. Hiding the console never stops the receiver.
  **View → Show Console on Startup** opens it automatically (off by default).
- **Connection status** at the top of the window shows *Receiver off*, *Starting*, *Disconnected*
  (waiting for devices), *Connecting*, *Connected* (device, address, stream type, duration) or *Error*.
  States come from events that `uxplay.exe` prints when the launcher sets `UXPLAY_GUI_STATUS=1`.
- **Errors** (device discovery unavailable, port in use, missing GStreamer plugin, invalid option, unexpected exit)
  are explained with a next step, a **Try again** button and the technical details.
- **Shortcuts:** `F5` start, `Shift+F5` stop, `Ctrl+Shift+F5` restart, `Ctrl+S` save, `Ctrl+1`–`Ctrl+4` pages,
  `Ctrl+L` open log, `F1` documentation, `Ctrl+Q` exit.
- **Multiple phones:** *Connection → Phones at once* (1–4) is a maximum. While every running receiver is in use,
  the next one ("UxPlay@PC 2", ...) starts automatically on base port + 10 each; extra idle receivers close again.
- **Player window:** sized to the video's aspect ratio (refits when the phone rotates; keeps the shape when you resize).
  Right-click it to rotate, reset rotation or fit it to the video.
- **Storage:** *Advanced → Storage* (and the installer) choose the settings and logs folders.
- **Closing** the window quits and stops every receiver (each runs in a kill-on-close job, so nothing is left
  behind even if the launcher crashes). **Minimizing** while running keeps it in the notification area.
  Only one launcher runs at a time.

| What | Where |
|---|---|
| Receiver settings | `%APPDATA%\UxPlay\uxplay-settings.ini` (imported once from the install folder if an older launcher saved it there) |
| Window size/position, theme, console preference | `%APPDATA%\UxPlay\launcher-prefs.ini` |
| Diagnostic log (receiver output + launcher events, 2 MB rotation, password never logged) | `%LOCALAPPDATA%\UxPlay\logs\uxplay.log` |

---

## Troubleshooting

### UxPlay doesn't appear in AirPlay list

1. **Check Windows Firewall:** Open **Windows Defender Firewall → Allow an app** and ensure `uxplay.exe` has both private and public network access (discovery uses UDP port 5353).
2. **Discovery:** the Connection page shows *Device discovery: Built-in* or *Apple Bonjour*. If another mDNS program misbehaves, try `UXPLAY_MDNS=builtin`, or stop the Bonjour Service in `services.msc`.
3. **Same network:** Confirm your iOS device and PC are on the same subnet. Guest Wi-Fi isolation blocks mDNS.
4. **Port conflict:** Try a different port with `-p 7200`.

### Black screen / no video

- Try software decode: `uxplay.bat -vd avdec_h264`
- Try a different video sink: `uxplay.bat -vs autovideosink`
- Check for GStreamer errors in the console output (run `uxplay.exe` directly from Command Prompt).

### Audio but no video (or vice versa)

- Ensure `libgstd3d11.dll` and `libgstlibav.dll` are present in `gst-plugins/`.
- Run from the terminal to see codec errors.

### App crashes on startup

- Run `uxplay.exe` from a Command Prompt to see the error message.
- Missing DLLs: ensure you're using `uxplay.bat` which sets `GST_PLUGIN_PATH`.

### NVIDIA d3d12videosink crash

Known issue with some NVIDIA driver versions. Use `d3d11videosink` instead:
```bat
uxplay.bat -vs d3d11videosink
```

### Server name with non-ASCII characters

Windows defaults to legacy code pages. To use UTF-8 characters in the server name, enable **UTF-8 (Beta)** in **Control Panel → Region → Administrative → Change system locale → Beta: Use Unicode UTF-8**.
