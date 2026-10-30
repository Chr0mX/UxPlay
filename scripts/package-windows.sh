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

DIST_DIR="${REPO_ROOT}/uxplay-windows-portable"
ZIP_NAME="uxplay-windows-portable.zip"

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

# ── Curated GStreamer plugins ─────────────────────────────────────────────────
# Ordered by importance. Optional ones are copied only if present.
REQUIRED_PLUGINS=(
  libgstcoreelements.dll       # essential: fakesink, filesink, queue, etc.
  libgstapp.dll                # appsrc/appsink — used directly by UxPlay
  libgstautodetect.dll         # autovideosink / autoaudiosink
  libgstplayback.dll           # playbin2 / playbin3
  libgsttypefindfunctions.dll  # format detection
  libgstvolume.dll             # volume element — used in UxPlay audio pipeline
  libgstlevel.dll              # level element — used unconditionally in UxPlay audio pipeline
  libgstvideoconvertscale.dll  # colorspace conversion + scaling
  libgstaudioconvert.dll       # audio format conversion
  libgstaudioresample.dll      # audio sample-rate conversion
  libgstd3d11.dll              # D3D11 video sink + DXVA2/D3D11VA hw decode
  libgstdirectsound.dll        # DirectSound audio output
  libgstvideoparsersbad.dll    # H.264 / H.265 / HEVC parsers
  libgstaudioparsers.dll       # AAC / AC3 / MP3 parsers
  libgstisomp4.dll             # MP4 / fragmented MP4 demuxer (HLS)
  libgstlibav.dll              # FFmpeg-based software decoders (fallback)
)

OPTIONAL_PLUGINS=(
  libgstopengl.dll             # OpenGL video sink (optional)
  libgstd3d12.dll              # D3D12 video sink (optional, newer GStreamer)
  libgstnvcodec.dll            # NVIDIA NVDEC hardware decode (optional)
  libgstamfcodec.dll           # AMD AMF hardware decode (optional)
  libgstqsvcodec.dll           # Intel QSV hardware decode (optional)
  libgstwasapi.dll             # Windows Audio Session API (alternative audio)
  libgstwasapi2.dll            # WASAPI2 audio sink (alternative audio)
  libgstasio.dll               # ASIO audio (optional)
  libgstvideofilter.dll        # Video filters (flip, rotate, etc.)
  libgstdeinterlace.dll        # Deinterlacing
  libgstinter.dll              # Inter-plugin communication
)

echo "==> Copying GStreamer plugins..."
for plugin in "${REQUIRED_PLUGINS[@]}"; do
  src="${UCRT64_GST_PLUGINS}/${plugin}"
  if [ -f "${src}" ]; then
    echo "    + ${plugin}"
    cp "${src}" "${DIST_DIR}/gst-plugins/${plugin}"
  else
    echo "    ! MISSING (required): ${plugin}"
    # Try to locate the plugin that provides the element name via gst-inspect-1.0
    element="${plugin#libgst}"       # strip libgst prefix
    element="${element%.dll}"         # strip .dll suffix
    found_dll=$(gst-inspect-1.0 "${element}" 2>/dev/null \
      | grep -i "Filename:" | awk '{print $2}' | head -1)
    if [ -n "${found_dll}" ] && [ -f "${found_dll}" ]; then
      found_name=$(basename "${found_dll}")
      echo "    ~ found via gst-inspect as ${found_name}"
      cp "${found_dll}" "${DIST_DIR}/gst-plugins/${found_name}"
    fi
  fi
done

for plugin in "${OPTIONAL_PLUGINS[@]}"; do
  src="${UCRT64_GST_PLUGINS}/${plugin}"
  if [ -f "${src}" ]; then
    echo "    + ${plugin} (optional)"
    cp "${src}" "${DIST_DIR}/gst-plugins/${plugin}"
  fi
done
plugin_count=$(find "${DIST_DIR}/gst-plugins" -maxdepth 1 -name '*.dll' | wc -l)
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
  echo "==> dnssd.dll not found: UxPlay will use its built-in mDNS responder (Bonjour is optional)."
fi

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

# ── Slim FFmpeg: keep only what UxPlay uses ──────────────────────────────────
# MSYS2's FFmpeg pulls in ~90 libraries UxPlay never uses (video encoders,
# subtitle rendering, speech recognition, ...). scripts/build-ffmpeg-slim.sh
# builds the same FFmpeg version with only the decoders UxPlay needs. Swap it in,
# drop every DLL nothing imports any more, and prove with gst-inspect (run from
# the package, with only Windows on PATH) that every plugin and element still
# loads and that the libav decoders UxPlay uses are there. On any doubt the full
# FFmpeg set is restored.
SLIM_BIN="${FFMPEG_SLIM_PREFIX:-/opt/ffmpeg-slim}/bin"

dll_imports() {   # lower-case DLL names a PE file imports
  objdump -p "$1" 2>/dev/null | awk '/DLL Name:/ {print tolower($3)}'
}

gst_inventory() {   # "<plugins> <features>" plus the libav decoder names, or empty if it cannot run
  local inspect="${DIST_DIR}/gst-inspect-slimcheck.exe" reg out
  cp "${UCRT64_BIN}/gst-inspect-1.0.exe" "${inspect}" || return 0
  reg="$(mktemp -d)/registry.bin"
  out=$(cd "${DIST_DIR}" && PATH="/c/Windows/System32:/c/Windows" \
        GST_PLUGIN_PATH="$(cygpath -w "${DIST_DIR}/gst-plugins")" GST_PLUGIN_SYSTEM_PATH="" \
        GST_PLUGIN_SCANNER="$(cygpath -w "${DIST_DIR}/gst-plugin-scanner.exe")" \
        GST_REGISTRY="$(cygpath -w "${reg}")" ./gst-inspect-slimcheck.exe 2>/dev/null)
  rm -f "${inspect}"
  echo "${out}" | grep 'Total count' | grep -o '[0-9][0-9]*' | tr '\n' ' '
  echo "${out}" | grep -o 'avdec_\(h264\|h265\|aac\|alac\)\b' | sort -u | tr '\n' ' '
}

if ls "${SLIM_BIN}"/avcodec-*.dll >/dev/null 2>&1; then
  echo "==> Slim FFmpeg: checking ${SLIM_BIN}"
  match=1
  for f in "${SLIM_BIN}"/*.dll; do
    [ -f "${DIST_DIR}/$(basename "${f}")" ] || { echo "    ! $(basename "${f}") is not in the package (version mismatch)"; match=0; }
  done
  before=$(gst_inventory)
  echo "    before: ${before:-gst-inspect could not run}"
  if [ "${match}" = 1 ] && [ -n "${before}" ]; then
    BACKUP="$(mktemp -d)"
    for f in "${SLIM_BIN}"/*.dll; do
      n=$(basename "${f}")
      mv "${DIST_DIR}/${n}" "${BACKUP}/${n}"
      cp "${f}" "${DIST_DIR}/${n}"
    done
    # Keep only DLLs reachable from the programs and plugins (dnssd.dll is loaded at run time).
    declare -A NEED=([dnssd.dll]=1)
    queue=("${DIST_DIR}/uxplay.exe" "${DIST_DIR}/uxplay-gui.exe" "${DIST_DIR}/gst-plugin-scanner.exe" \
           "${DIST_DIR}"/gst-plugins/*.dll)
    while [ "${#queue[@]}" -gt 0 ]; do
      f="${queue[0]}"; queue=("${queue[@]:1}")
      [ -f "${f}" ] || continue
      for d in $(dll_imports "${f}"); do
        [ -n "${NEED[$d]:-}" ] && continue
        actual=$(find "${DIST_DIR}" -maxdepth 1 -iname "${d}" | head -1)
        [ -n "${actual}" ] || continue   # system DLL
        NEED[$d]=1
        queue+=("${actual}")
      done
    done
    removed=0
    for f in "${DIST_DIR}"/*.dll; do
      n=$(basename "${f}")
      if [ -z "${NEED[${n,,}]:-}" ]; then mv "${f}" "${BACKUP}/"; removed=$((removed + 1)); fi
    done
    after=$(gst_inventory)
    echo "    after:  ${after:-gst-inspect could not run}"
    if [ -n "${after}" ] && [ "${after}" = "${before}" ] && [[ "${after}" == *avdec_h264* ]]; then
      echo "    Slim FFmpeg in use: ${removed} unused DLLs removed"
    else
      echo "::warning::slim FFmpeg check failed; keeping the full FFmpeg"
      echo "    ! check failed; restoring the full FFmpeg"
      for n in "${SLIM_BIN}"/*.dll; do rm -f "${DIST_DIR}/$(basename "${n}")"; done
      mv "${BACKUP}"/*.dll "${DIST_DIR}/"
    fi
    rm -rf "${BACKUP}"
  else
    echo "    ! not used (see above); keeping the full FFmpeg"
  fi
else
  echo "==> Slim FFmpeg not built; keeping MSYS2's full FFmpeg"
fi
echo "==> Package size: $(du -sh "${DIST_DIR}" | cut -f1)"

# ── Write user README ─────────────────────────────────────────────────────────
cat > "${DIST_DIR}/README.md" << READMEEOF
# UxPlay ${VERSION} for Windows

UxPlay turns your PC into an AirPlay receiver: mirror an iPhone, iPad or Mac screen to Windows.

## Quick start

1. Open **UxPlay** (\`uxplay-gui.exe\`) and press **Start**. Allow it through Windows Firewall if asked. Nothing else to install (no Bonjour or iTunes needed).
2. On your iPhone or iPad, open **Control Center → Screen Mirroring** and pick the name UxPlay shows.

## What's in this folder

| File | What it is |
|---|---|
| \`uxplay-gui.exe\` | **UxPlay: open this one.** The window with settings, status and the Start button. |
| \`uxplay.exe\` | The receiver engine. UxPlay starts it for you in the background, one per phone; you never need to open it. Task Manager shows it as *UxPlay receiver engine*. It can also be run from a Command Prompt with your own options (advanced). |

## The UxPlay window

- **Status** at the top shows whether a device is connected, which one, and for how long.
- **Phones at once** (Connection page): while a phone is mirroring, another receiver
  ("*name* 2", ...) appears for the next phone, each in its own window.
- **Player window:** right-click it to rotate the picture or fit the window to the video.
  It keeps the video's shape when you resize it.
- **Console** (View → Show Console, Ctrl+\`) shows the receiver's output; it is hidden by default.
- **Storage** (Advanced page): choose where settings and logs are saved.
- Minimizing keeps the receiver running in the notification area; closing the window quits UxPlay.

## Command line

Open a Command Prompt in this folder and run \`uxplay.exe\` with options, for example
\`uxplay.exe -n "Living Room" -fs\`. It finds the bundled GStreamer by itself.

| Option | Meaning |
|---|---|
| \`-n <name>\` | AirPlay receiver name |
| \`-fs\` | Start fullscreen |
| \`-s WxH\` | Window size, e.g. \`-s 1920x1080\` |
| \`-p <port>\` | Base network port (default 7100) |
| \`-vs <sink>\` | Video sink: d3d11videosink, d3d12videosink, autovideosink |
| \`-vd <decoder>\` | Video decoder: d3d11videodec, nvh264dec, avdec_h264 |
| \`-as <sink>\` | Audio sink: directsoundsink, wasapisink, autoaudiosink |

## Troubleshooting

- **Not in the AirPlay list:** make sure Windows Firewall allows UxPlay (discovery uses UDP port
  5353) and that the PC and phone are on the same network. Apple Bonjour is not needed: UxPlay
  has its own discovery and uses Bonjour only if it is already installed.
- **Black screen / no video:** set the video decoder to *Software (FFmpeg)* on the Display page.
- **Audio problems:** try a different audio sink on the Audio page.

Requires Windows 10 or 11 (64-bit). Full documentation: docs/WINDOWS.md and the project README.
READMEEOF

# ── Zip the portable folder (also the tree NSIS packages into the installer) ──
cd "${REPO_ROOT}"
if command -v zip &>/dev/null; then
  zip -r "${ZIP_NAME}" uxplay-windows-portable/ -x "*.missing"
else
  # Fallback: use 7-Zip (always available on Windows GitHub runners)
  7z a -tzip "${ZIP_NAME}" ./uxplay-windows-portable/ -xr!"*.missing"
fi
echo "==> Done: ${ZIP_NAME} ($(du -sh "${ZIP_NAME}" | cut -f1))"
