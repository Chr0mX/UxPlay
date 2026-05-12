# Windows Portable Build Guide

This guide is for **end users** running the prebuilt `uxplay-portable.zip` on Windows, without installing MSYS2.

## Download, unzip, and run

1. Download `uxplay-portable.zip` from the UxPlay release assets.
2. Right-click the ZIP, choose **Extract All...**, and extract to a user-writable path (for example `C:\Tools\UxPlay`).
3. Open the extracted folder and run `uxplay.exe`.
4. Optional: create a shortcut to `uxplay.exe` and set preferred launch arguments in shortcut properties.

> Tip: avoid running directly from inside the ZIP file. Always extract first.

## Firewall and network prompts

On first launch, Windows Defender Firewall may prompt for network access for `uxplay.exe`.

- Allow access on **Private networks** at minimum (home/office LAN).
- Public network access is usually unnecessary unless you intentionally use UxPlay on public networks.
- If you dismiss the prompt, add the app manually in:
  - **Settings → Privacy & security → Windows Security → Firewall & network protection → Allow an app through firewall**.

AirPlay discovery/control and streaming generally require local-network reachability between sender and receiver.

## Bonjour expectations and alternate discovery mode

UxPlay supports two discovery paths on Windows:

1. **Bonjour / DNS-SD path** (common default)
   - Expected behavior: if Bonjour service is installed and running, Apple devices discover UxPlay via local DNS-SD.
   - If discovery fails, verify Bonjour service status in `services.msc`.

2. **Bluetooth LE beacon path** (alternate discovery mode)
   - Used where DNS-SD/Bonjour multicast is filtered or unavailable.
   - Expected behavior: discovery is provided by Bluetooth LE advertisement instead of (or alongside) Bonjour.
   - Requires a compatible Bluetooth LE setup/module as documented in project beacon instructions.

If both network and BLE environments are restricted, discovery can fail even when `uxplay.exe` itself starts correctly.

## Known limitations

- Portable builds are intended for **Windows 10/11 64-bit**.
- Some enterprise endpoint policies can block unsigned binaries, local discovery, multicast, or inbound prompts.
- Video/audio behavior depends on available GPU drivers, audio devices, and GStreamer runtime components packaged with the build.
- Screen-capture-protected apps/services on sender devices may refuse mirroring by DRM policy.

## Troubleshooting

### UxPlay is not discovered by iPhone/iPad/Mac

- Confirm sender and PC are on the same LAN/VLAN.
- Confirm Windows firewall allows `uxplay.exe` on private networks.
- If using Bonjour mode, verify Bonjour service is running.
- If using BLE discovery mode, verify Bluetooth adapter/module and beacon setup.

### App launches but mirror window is black or unstable

- Update GPU drivers.
- Try an alternate videosink argument (for example D3D11 vs D3D12, depending on host capability).
- Test windowed mode before fullscreen.

### Audio is missing or delayed

- Try switching output sink/device (for example WASAPI-based output options).
- Check default output device and sample-rate settings in Windows Sound settings.
- Close exclusive-mode audio applications that can seize the target device.

### Antivirus or SmartScreen blocks launch

- Verify binary source (official release artifact).
- Use **More info → Run anyway** only when provenance is trusted.
- In managed environments, request allowlisting by IT.
