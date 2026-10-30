#!/usr/bin/env bash
# Builds a small FFmpeg for the Windows package (run in MSYS2 UCRT64).
#
# GStreamer's FFmpeg plugin (gst-libav) links MSYS2's FFmpeg, which is built
# with ~90 extra libraries (x264/x265/AV1/VP9 encoders, librsvg, libplacebo,
# whisper, Blu-ray, SSH, ...). UxPlay only uses FFmpeg's decoders for H.264,
# H.265, AAC(-ELD) and ALAC (plus a few common audio formats for HLS video).
# This builds the *same FFmpeg version* (same DLL names and ABI, so gst-libav
# loads it unchanged) with only those decoders and no external libraries.
# package-windows.sh swaps it in and drops the libraries nothing needs any more.
#
# Output: ${FFMPEG_SLIM_PREFIX:-/opt/ffmpeg-slim}/bin/*.dll
# Never fails the build: if the matching source cannot be fetched or built,
# it prints a warning and the package keeps MSYS2's full FFmpeg.

set -uo pipefail

PREFIX="${FFMPEG_SLIM_PREFIX:-/opt/ffmpeg-slim}"
PKG="mingw-w64-ucrt-x86_64-ffmpeg"

warn() { echo "::warning::slim FFmpeg: $*"; echo "WARNING: slim FFmpeg: $*"; exit 0; }

# MSYS2 package version, e.g. "8.0.1-2" or "2~8.0.1-1" -> "8.0.1"
VER=$(pacman -Q "${PKG}" 2>/dev/null | awk '{print $2}')
[ -n "${VER}" ] || warn "${PKG} is not installed"
VER="${VER#*~}"
VER="${VER%-*}"
echo "==> MSYS2 FFmpeg version: ${VER}"

if [ -f "${PREFIX}/.built-${VER}" ]; then
  echo "==> slim FFmpeg ${VER} already built (cache)"
  exit 0
fi

WORK="$(mktemp -d)"
cd "${WORK}" || warn "no temp dir"
URL="https://ffmpeg.org/releases/ffmpeg-${VER}.tar.xz"
echo "==> Downloading ${URL}"
curl -fsSL --retry 3 -o ffmpeg.tar.xz "${URL}" || warn "could not download ${URL}"
tar xf ffmpeg.tar.xz || warn "could not unpack the source"
cd "ffmpeg-${VER}" || warn "unexpected source layout"

DECODERS="h264,hevc,aac,aac_latm,alac,mp3,mp3float,ac3,eac3,opus,flac,pcm_s16le,pcm_s16be,pcm_f32le"
PARSERS="h264,hevc,aac,aac_latm,mpegaudio,ac3,opus,flac"

echo "==> Configuring (decoders: ${DECODERS})"
./configure \
  --prefix="${PREFIX}" \
  --enable-shared --disable-static \
  --disable-programs --disable-doc \
  --disable-autodetect --disable-network --disable-debug \
  --disable-everything \
  --enable-decoder="${DECODERS}" \
  --enable-parser="${PARSERS}" \
  --enable-avcodec --enable-avformat --enable-avfilter \
  --enable-swresample --enable-swscale \
  > configure.log 2>&1 || { tail -30 configure.log; warn "configure failed"; }

echo "==> Building"
make -j"$(nproc)" > make.log 2>&1 || { tail -30 make.log; warn "build failed"; }
rm -rf "${PREFIX}"
make install > install.log 2>&1 || { tail -30 install.log; warn "install failed"; }
strip --strip-unneeded "${PREFIX}"/bin/*.dll 2>/dev/null || true
touch "${PREFIX}/.built-${VER}"
echo "==> slim FFmpeg ${VER} in ${PREFIX}/bin:"
ls -la "${PREFIX}/bin"/*.dll
