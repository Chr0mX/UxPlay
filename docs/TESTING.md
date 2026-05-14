# UxPlay Windows — Testing Checklist

Use this checklist to validate a portable Windows release on a clean system.

## Test Environment Setup

- **Clean VM:** Windows 10 22H2 or Windows 11 23H2 (no iTunes, iCloud, or MSYS2 installed)
- **iOS Device:** iPhone or iPad running iOS 14+ / iPadOS 14+
- **Network:** Both PC and iOS device on the same 5 GHz Wi-Fi network
- **GPU options to cover:** Intel integrated, AMD discrete/integrated, NVIDIA discrete

---

## 1. ZIP Extraction & File Integrity

- [ ] Download `uxplay-windows-<version>.zip` from the Releases page
- [ ] Extract with Windows built-in extractor (right-click → Extract All)
- [ ] Verify the following files are present in the extracted folder:
  - [ ] `uxplay.exe`
  - [ ] `uxplay.bat`
  - [ ] `README-Windows.txt`
  - [ ] `dnssd.dll` (or note that Bonjour must be installed separately)
  - [ ] `gst-plugins/` directory with at least 10 `.dll` files
  - [ ] `libgstreamer-1.0-0.dll` (or similar) in root

---

## 2. Bonjour / mDNS Prerequisite

- [ ] **With no Bonjour installed:** run `uxplay.exe` — confirm it prints a useful error
  (`DNSSD_ERROR_LIBNOTFOUND`) rather than crashing silently
- [ ] **Install Bonjour Print Services** from Apple, then retest
- [ ] Verify Bonjour service is running: `services.msc` → **Bonjour Service** = Running

---

## 3. First Launch

- [ ] Double-click `uxplay.bat` — terminal window opens with UxPlay startup log
- [ ] **Windows Defender Firewall prompt appears** — click "Allow access"
- [ ] UxPlay prints the server name and listening port (e.g. `Listening on port 7100`)
- [ ] No crash, no immediate exit

---

## 4. AirPlay Discovery

- [ ] On iOS, open **Control Center → Screen Mirroring**
- [ ] The UxPlay server name appears in the list within ~10 seconds
- [ ] Tap the server name — connection is accepted
- [ ] UxPlay console shows "Client connected" or similar

---

## 5. Screen Mirroring — Basic

- [ ] Screen mirroring starts within 3 seconds of connecting
- [ ] Video renders (not black, not green, not frozen)
- [ ] Audio plays through PC speakers/headphones
- [ ] No significant lag (target: < 300 ms latency on 5 GHz Wi-Fi)
- [ ] Disconnect from iOS side — UxPlay returns to waiting state cleanly

---

## 6. Video Quality & Fullscreen

- [ ] `-fs` flag: `uxplay.bat -fs` — starts in fullscreen
- [ ] Rotate iOS device — mirrored display rotates accordingly
- [ ] `-s 1920x1080`: `uxplay.bat -s 1920x1080` — window/output is correct size
- [ ] Window close button exits UxPlay cleanly (no crash, no hanging process)

---

## 7. Hardware Decoding — Intel / AMD

*Requires Intel HD/Iris/Arc or AMD Radeon GPU.*

- [ ] `uxplay.bat -vd d3d11videodec -vs d3d11videosink`
  - [ ] Video renders correctly
  - [ ] CPU usage is lower than software decode
  - [ ] No crash / green frames / corruption
- [ ] H.265/HEVC (if supported by device): `uxplay.bat -vd d3d11videodec`
  - [ ] Streams from iOS without error

---

## 8. Hardware Decoding — NVIDIA

*Requires NVIDIA GPU with NVDEC (GTX 600+ for H.264, GTX 900+ for H.265).*

- [ ] `uxplay.bat -vd nvh264dec -vs d3d11videosink`
  - [ ] Video renders correctly
  - [ ] `libgstnvcodec.dll` is present in `gst-plugins/`
  - [ ] No segfault or DLL error
- [ ] H.265: `uxplay.bat -vd nvh265dec -vs d3d11videosink`

---

## 9. D3D12 Video Sink

- [ ] `uxplay.bat -vs d3d12videosink`
  - [ ] Video renders
  - [ ] **Alt+Enter** toggles fullscreen (note: requires DirectX 12 capable GPU)
  - [ ] On older NVIDIA: verify no crash; if crash occurs, document and recommend `d3d11videosink`

---

## 10. Software Decoding (Fallback)

- [ ] `uxplay.bat -vd avdec_h264 -vs d3d11videosink`
  - [ ] Video renders (higher CPU, no GPU required)
  - [ ] Useful as regression check if hardware decode fails

---

## 11. Audio-Only Streaming

- [ ] On iOS: play music and use AirPlay to send **audio only** (not screen mirroring)
- [ ] Audio plays on PC
- [ ] No video window opens (expected behavior)
- [ ] `uxplay.bat -as wasapisink` — audio works with WASAPI sink

---

## 12. Multi-Monitor

- [ ] Connect a second display
- [ ] `uxplay.bat -vs "d3d11videosink display-index=1"` — video appears on second monitor
- [ ] `uxplay.bat -fs -vs "d3d11videosink display-index=1"` — fullscreen on second monitor

---

## 13. Authentication Options

- [ ] `uxplay.bat -pin` — iOS prompts for PIN; enter it; connection succeeds
- [ ] `uxplay.bat -pw testpass` — iOS prompts for password; connection succeeds
- [ ] Wrong password: connection is rejected cleanly

---

## 14. Regression — Linux Build

*Run on a Linux system or in CI.*

- [ ] `cmake -B build -G Ninja && cmake --build build` succeeds on Ubuntu 22.04+
- [ ] No CMake errors related to `dnssd.lib` or `dns_sd.h` changes
- [ ] Linux binary starts and registers mDNS service (check with `avahi-browse -r _airplay._tcp`)

---

## Known Issues & Notes

| Issue | Workaround |
|-------|-----------|
| NVIDIA + d3d12videosink crash | Use `-vs d3d11videosink` |
| Non-ASCII server name garbled | Enable UTF-8 Beta in Windows Region settings |
| AirPlay not visible on guest Wi-Fi | Use main network; guest networks block mDNS |
| High CPU with no hardware decode | Add `-vd d3d11videodec` flag |
| `dnssd.dll` not found error | Install Apple Bonjour for Windows |

---

## Reporting Issues

When filing a bug report, include:

1. Windows version (`winver`)
2. GPU model and driver version
3. UxPlay version and launch command used
4. Full console output (run `uxplay.exe` directly from Command Prompt)
5. iOS/iPadOS version
6. Whether the issue is reproducible on software decode (`-vd avdec_h264`)
