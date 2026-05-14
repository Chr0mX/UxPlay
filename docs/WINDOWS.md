# UxPlay on Windows

UxPlay runs natively on Windows 10 and 11 (64-bit) using GStreamer for audio/video rendering and Apple Bonjour for AirPlay device discovery. A portable ZIP release is available that requires no installation of MSYS2, Python, or package managers.

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
3. **Install Bonjour** if not already present (see [Prerequisites](#prerequisites)).
4. Double-click **`uxplay.bat`** or open a Command Prompt in the folder and run:
   ```
   uxplay.bat
   ```
5. When Windows Firewall prompts, click **Allow access** on both private and public networks.
6. On your iPhone/iPad: open **Control Center → Screen Mirroring** and tap the device name shown in UxPlay's title bar.

---

## Prerequisites

### Apple Bonjour (required)

UxPlay uses Apple's mDNS/DNS-SD protocol for AirPlay device discovery. The Bonjour service (`mDNSResponder`) must be running on Windows.

Bonjour is automatically installed by:
- **iTunes for Windows**
- **iCloud for Windows**
- **Apple TV app**
- **Apple Devices app** (Windows Store)

If none of the above are installed, get the standalone installer:
- Download [Bonjour Print Services for Windows](https://support.apple.com/downloads/Bonjour) from Apple (free).
- Run `BonjourPSSetup.exe` and follow the prompts.
- Verify it's running: open **Task Manager → Services** and check for `mDNSResponder` or open **services.msc** and look for **Bonjour Service**.

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

> **Note:** `dns_sd.h` is bundled in `deps/windows/` — no Bonjour SDK installer is required to build. At runtime, the Bonjour service is still needed (see [Prerequisites](#prerequisites)).

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

Allow Windows Firewall access when prompted. The Bonjour service must be running.

### 5. Create portable package (optional)

```bash
bash scripts/package-windows.sh
```

This produces `uxplay-windows-<version>.zip` with all required DLLs bundled.

---

## Troubleshooting

### UxPlay doesn't appear in AirPlay list

1. **Check Bonjour service:** Open `services.msc` and verify **Bonjour Service** is running. Start it if stopped.
2. **Check Windows Firewall:** Open **Windows Defender Firewall → Allow an app** and ensure `uxplay.exe` has both private and public network access.
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
