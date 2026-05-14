#!/usr/bin/env bash
# package-windows.sh — Build portable Windows ZIP for UxPlay
# Run inside MSYS2 UCRT64 environment after cmake --build build
set -euo pipefail

UCRT64_BIN="/ucrt64/bin"
UCRT64_GST_PLUGINS="/ucrt64/lib/gstreamer-1.0"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build"

# Read version from CMakeLists.txt (fallback to git tag or "dev")
VERSION=$(grep -m1 'set.*UXPLAY_VERSION\|project.*VERSION' "${REPO_ROOT}/CMakeLists.txt" \
  | grep -oP '[0-9]+\.[0-9]+(\.[0-9]+)?' | head -1 || true)
if [ -z "${VERSION}" ]; then
  VERSION=$(git -C "${REPO_ROOT}" describe --tags --abbrev=0 2>/dev/null | tr -d 'v' || echo "dev")
fi

DIST_DIR="${REPO_ROOT}/dist"
ZIP_NAME="uxplay-windows-${VERSION}.zip"

echo "==> Packaging UxPlay ${VERSION} for Windows"
echo "    UCRT64_BIN:         ${UCRT64_BIN}"
echo "    UCRT64_GST_PLUGINS: ${UCRT64_GST_PLUGINS}"
echo "    OUTPUT:             ${ZIP_NAME}"

# ── Clean slate ────────────────────────────────────────────────────────────────
rm -rf "${DIST_DIR}"
mkdir -p "${DIST_DIR}/gst-plugins"

# ── Copy main binary ──────────────────────────────────────────────────────────
cp "${BUILD_DIR}/uxplay.exe" "${DIST_DIR}/uxplay.exe"

# ── Windows system DLLs to exclude from bundling ─────────────────────────────
# These are provided by the OS on any supported Windows version.
SYSTEM_DLLS=(
  advapi32.dll avrt.dll bcrypt.dll cfgmgr32.dll combase.dll comctl32.dll
  comdlg32.dll crypt32.dll d3d10.dll d3d10_1.dll d3d11.dll d3d12.dll
  ddraw.dll dhcpcsvc.dll dinput8.dll dwmapi.dll dxgi.dll gdi32.dll
  hid.dll imm32.dll iphlpapi.dll kernel32.dll kernelbase.dll
  mf.dll mfplat.dll mfreadwrite.dll mmdevapi.dll msacm32.dll
  msvcrt.dll mswsock.dll netapi32.dll ntdll.dll ole32.dll oleaut32.dll
  opengl32.dll pdh.dll powrprof.dll psapi.dll rpcrt4.dll secur32.dll
  setupapi.dll shell32.dll shlwapi.dll user32.dll userenv.dll
  uxtheme.dll version.dll winmm.dll winspool.drv wintrust.dll
  ws2_32.dll wsock32.dll wtsapi32.dll
)

is_system_dll() {
  local dll="${1,,}"  # lowercase
  for sys in "${SYSTEM_DLLS[@]}"; do
    [[ "${dll}" == "${sys}" ]] && return 0
  done
  return 1
}

# ── Collect runtime DLLs via ntldd ───────────────────────────────────────────
echo "==> Resolving DLL dependencies with ntldd..."
ntldd -R "${DIST_DIR}/uxplay.exe" 2>/dev/null \
  | awk '{print $3}' \
  | grep -i "ucrt64" \
  | sort -u \
  | while read -r dll_path; do
      dll_name=$(basename "${dll_path}")
      if ! is_system_dll "${dll_name}"; then
        echo "    + ${dll_name}"
        cp "${dll_path}" "${DIST_DIR}/${dll_name}"
      fi
    done

# ── GStreamer plugins — copy ALL of them ─────────────────────────────────────
# A curated list misses elements as UxPlay evolves; copying everything is
# robust, and GStreamer gracefully skips plugins whose hardware isn't present.
echo "==> Copying all GStreamer plugins from ${UCRT64_GST_PLUGINS}..."
plugin_count=0
for src in "${UCRT64_GST_PLUGINS}"/libgst*.dll; do
  [ -f "${src}" ] || continue
  plugin_name=$(basename "${src}")
  cp "${src}" "${DIST_DIR}/gst-plugins/${plugin_name}"
  echo "    + ${plugin_name}"
  plugin_count=$((plugin_count + 1))
done
echo "    ${plugin_count} plugins copied."

# GStreamer plugins also depend on DLLs; collect them too
echo "==> Resolving GStreamer plugin DLL dependencies..."
find "${DIST_DIR}/gst-plugins" -name "*.dll" | while read -r plugin_dll; do
  ntldd -R "${plugin_dll}" 2>/dev/null \
    | awk '{print $3}' \
    | grep -i "ucrt64" \
    | while read -r dep_path; do
        dep_name=$(basename "${dep_path}")
        if ! is_system_dll "${dep_name}" && [ ! -f "${DIST_DIR}/${dep_name}" ]; then
          echo "    + ${dep_name} (plugin dep)"
          cp "${dep_path}" "${DIST_DIR}/${dep_name}"
        fi
      done
done

# libgstcodec2json.dll may depend on DLLs outside the ucrt64 tree;
# resolve without the path filter so its deps are always bundled.
if [ -f "${DIST_DIR}/gst-plugins/libgstcodec2json.dll" ]; then
  echo "==> Resolving libgstcodec2json.dll dependencies (no path filter)..."
  ntldd -R "${DIST_DIR}/gst-plugins/libgstcodec2json.dll" 2>/dev/null \
    | awk '$3 ~ /\// { print $3 }' \
    | while read -r dep_path; do
        [ -f "${dep_path}" ] || continue
        dep_name=$(basename "${dep_path}")
        if ! is_system_dll "${dep_name}" && [ ! -f "${DIST_DIR}/${dep_name}" ]; then
          echo "    + ${dep_name} (codec2json dep)"
          cp "${dep_path}" "${DIST_DIR}/${dep_name}"
        fi
      done || true
fi

# ── Copy GStreamer plugin scanner + its own DLL deps ─────────────────────────
GST_SCANNER="${UCRT64_BIN}/gst-plugin-scanner.exe"
if [ -f "${GST_SCANNER}" ]; then
  cp "${GST_SCANNER}" "${DIST_DIR}/gst-plugin-scanner.exe"
  echo "==> Resolving gst-plugin-scanner.exe DLL dependencies..."
  ntldd -R "${DIST_DIR}/gst-plugin-scanner.exe" 2>/dev/null \
    | awk '{print $3}' \
    | grep -i "ucrt64" \
    | while read -r dep_path; do
        dep_name=$(basename "${dep_path}")
        if ! is_system_dll "${dep_name}" && [ ! -f "${DIST_DIR}/${dep_name}" ]; then
          echo "    + ${dep_name} (scanner dep)"
          cp "${dep_path}" "${DIST_DIR}/${dep_name}"
        fi
      done
fi

# ── Copy dnssd.dll (Bonjour) if available ─────────────────────────────────────
DNSSD_SRC="${SCRIPT_DIR}/dnssd.dll"
if [ -f "${DNSSD_SRC}" ]; then
  echo "==> Copying dnssd.dll (Bonjour)"
  cp "${DNSSD_SRC}" "${DIST_DIR}/dnssd.dll"
elif [ -f "/c/Windows/System32/dnssd.dll" ]; then
  echo "==> Copying dnssd.dll from System32"
  cp "/c/Windows/System32/dnssd.dll" "${DIST_DIR}/dnssd.dll"
else
  echo "==> WARNING: dnssd.dll not found. Bonjour must be installed separately."
fi

# ── Create launcher batch script ──────────────────────────────────────────────
cat > "${DIST_DIR}/uxplay.bat" << 'BATCHEOF'
@echo off
setlocal

rem UxPlay launcher — sets GStreamer environment then starts uxplay.exe
set "UXPLAY_DIR=%~dp0"

rem Point GStreamer at the bundled plugins; store registry next to the exe
rem so it rebuilds automatically when the package is run for the first time.
set "GST_PLUGIN_PATH=%UXPLAY_DIR%gst-plugins"
set "GST_PLUGIN_SCANNER=%UXPLAY_DIR%gst-plugin-scanner.exe"
set "GST_REGISTRY=%UXPLAY_DIR%gstreamer-registry.bin"
rem Prevent GStreamer from searching a non-existent system installation
set "GST_PLUGIN_SYSTEM_PATH="
set "PATH=%UXPLAY_DIR%;%PATH%"

rem Pass all arguments through to uxplay.exe
"%UXPLAY_DIR%uxplay.exe" %*
BATCHEOF

# ── Copy GUI launcher ─────────────────────────────────────────────────────────
if [ -f "${BUILD_DIR}/gui/uxplay-gui.exe" ]; then
  cp "${BUILD_DIR}/gui/uxplay-gui.exe" "${DIST_DIR}/uxplay-gui.exe"
  echo "==> Copied uxplay-gui.exe"
  echo "==> Resolving uxplay-gui.exe DLL dependencies..."
  ntldd -R "${DIST_DIR}/uxplay-gui.exe" 2>/dev/null \
    | awk '{print $3}' \
    | grep -i "ucrt64" \
    | while read -r dep_path; do
        dep_name=$(basename "${dep_path}")
        if ! is_system_dll "${dep_name}" && [ ! -f "${DIST_DIR}/${dep_name}" ]; then
          echo "    + ${dep_name} (gui dep)"
          cp "${dep_path}" "${DIST_DIR}/${dep_name}"
        fi
      done || true
else
  echo "==> NOTE: uxplay-gui.exe not found in build/gui/ — skipping GUI launcher"
fi
# ── Copy Bluetooth beacon script (Windows module) ────────────────────────────
BEACON_DIR="${REPO_ROOT}/Bluetooth_LE_beacon"
if [ -d "${BEACON_DIR}" ]; then
  cp "${BEACON_DIR}/uxplay-beacon.py" "${DIST_DIR}/" 2>/dev/null || true
  cp "${BEACON_DIR}/uxplay_beacon_module_winrt.py" "${DIST_DIR}/" 2>/dev/null || true
fi

# ── Write user README ─────────────────────────────────────────────────────────
cat > "${DIST_DIR}/README-Windows.txt" << READMEEOF
UxPlay ${VERSION} — Portable Windows Release
=============================================

QUICK START
-----------
1. Install Apple Bonjour for Windows if not already installed:
   - Bonjour is bundled with iTunes, iCloud for Windows, and Apple TV.
   - Standalone: https://support.apple.com/downloads/Bonjour

2. Run uxplay.bat (recommended) or uxplay.exe directly.
   Windows will ask for firewall permission on first launch — click Allow.

3. On your iPhone/iPad, open Control Center, tap Screen Mirroring,
   and select the device name shown in the UxPlay window.

HARDWARE DECODING (Recommended for smooth video)
-------------------------------------------------
Intel / AMD (DXVA2 / D3D11VA):
  uxplay.bat -vd d3d11videodec -vs d3d11videosink

NVIDIA (NVDEC):
  uxplay.bat -vd nvh264dec -vs d3d11videosink

D3D12 sink with Alt+Enter fullscreen toggle:
  uxplay.bat -vs d3d12videosink

SOFTWARE DECODING (fallback, no GPU requirement):
  uxplay.bat -vd avdec_h264 -vs d3d11videosink

USEFUL OPTIONS
--------------
  -n <name>        Set AirPlay receiver name (default: hostname)
  -fs              Start in fullscreen
  -s WxH           Set window size (e.g. -s 1920x1080)
  -p <port>        Set AirPlay port (default: 7100)
  -vs <sink>       Video sink: d3d11videosink, d3d12videosink, autovideosink
  -as <sink>       Audio sink: directsoundsink, wasapisink, autoaudiosink
  -vd <decoder>    Video decoder: d3d11videodec, nvh264dec, avdec_h264
  -v               Verbose logging

REQUIREMENTS
------------
- Windows 10 or 11 (64-bit)
- Apple Bonjour service (mDNSResponder) — see QUICK START above
- Network: device and iPhone/iPad on the same Wi-Fi network

TROUBLESHOOTING
---------------
- Not visible in AirPlay list: check Windows Firewall, ensure Bonjour
  service is running (services.msc → "Bonjour Service")
- Black screen / no video: try software decode (-vd avdec_h264)
- Audio issues: try -as wasapisink or -as directsoundsink
- Crashes with d3d12videosink + old Nvidia: add -vs d3d11videosink instead

For full documentation, see docs/WINDOWS.md or the project README.
READMEEOF

# ── Create ZIP ────────────────────────────────────────────────────────────────
echo "==> Creating ${ZIP_NAME}..."
cd "${REPO_ROOT}"
if command -v zip &>/dev/null; then
  zip -r "${ZIP_NAME}" dist/ -x "*.missing"
else
  # Fallback: use 7-Zip (always available on Windows GitHub runners)
  7z a -tzip "${ZIP_NAME}" ./dist/ -xr!"*.missing"
fi
echo "==> Done: ${ZIP_NAME} ($(du -sh "${ZIP_NAME}" | cut -f1))"
