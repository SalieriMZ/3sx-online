#!/usr/bin/env bash
# Build a minimal FFmpeg n7.0.2 for PlayStation Vita (arm-vita-eabi) using
# the vitasdk toolchain. Produces static libavcodec / libswresample / libavutil
# with the adpcm_adx decoder + adx parser enabled.
#
# Background: vitasdk's prebuilt /usr/local/vitasdk/arm-vita-eabi/lib/libav*.a
# is compiled WITHOUT --enable-decoder=adpcm_adx, so
# avcodec_find_decoder(AV_CODEC_ID_ADPCM_ADX) returns NULL and
# track_init NULL-derefs in avcodec_open2 (crash5.psp2dmp). This recipe
# rebuilds ffmpeg with adpcm_adx + adx parser so SOUND_ENABLED can boot.
#
# Designed to run inside the vitasdk/vitasdk:latest docker image (alpine).
# Invoke from the project root via Git Bash on Windows:
#   docker run --rm -v "$(pwd -W):/workspace" -w /workspace \
#       vitasdk/vitasdk:latest bash build-ffmpeg-vita.sh
#
# Outputs:
#   third_party/ffmpeg/build-vita-arm/lib/{libavcodec.a,libswresample.a,libavutil.a}
#   third_party/ffmpeg/build-vita-arm/include/{libavcodec,libswresample,libavutil}/...
#
# Pitfalls handled:
#   * --disable-pic: vita-elf-create rejects R_ARM_BASE_PREL / R_ARM_GOT_BREL.
#   * --disable-neon: ffmpeg's NEON auto-detect is unreliable on this cross
#     target; explicit asm in adpcm_adx_decoder doesn't need it anyway.
#   * Alpine container has no host-cc; we apk-add gcc + musl-dev for the few
#     build-time helpers ffmpeg's configure compiles natively. On a Windows
#     host outside docker, scoop's gcc at ~/scoop/apps/gcc/current/bin/gcc.exe
#     serves the same role (see HOST_CC fallback below).

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
THIRD_PARTY="$ROOT_DIR/third_party"

: "${VITASDK:?VITASDK must be set (point at vitasdk install root)}"

BUILD_JOBS="${BUILD_JOBS:-$(nproc 2>/dev/null || echo 4)}"
echo "Using $BUILD_JOBS build job(s)"
echo "VITASDK=$VITASDK"

# -----------------------------
# FFmpeg n7.0.2 (Vita)
# -----------------------------

FFMPEG_REF="n7.0.2"
FFMPEG_SRC="$THIRD_PARTY/ffmpeg/src-vita-arm"
FFMPEG_BUILD="$THIRD_PARTY/ffmpeg/build-vita-arm"

if [ -f "$FFMPEG_BUILD/lib/libavcodec.a" ]; then
    echo "FFmpeg (vita) already built at $FFMPEG_BUILD"
    exit 0
fi

# Pick a host C compiler for configure's build-time helpers. Inside the
# vitasdk alpine container there is no native gcc by default, so install one
# via apk if needed. On a normal Linux host we use whatever is on PATH; on
# Windows + Git Bash without docker, point HOST_CC at scoop's gcc.
if [ -z "${HOST_CC:-}" ]; then
    if command -v gcc >/dev/null 2>&1; then
        HOST_CC="$(command -v gcc)"
    elif command -v cc >/dev/null 2>&1; then
        HOST_CC="$(command -v cc)"
    elif command -v apk >/dev/null 2>&1; then
        echo "No host gcc found in container; installing via apk..."
        apk add --no-cache gcc musl-dev make >/dev/null
        HOST_CC="$(command -v gcc)"
    elif [ -x "$HOME/scoop/apps/gcc/current/bin/gcc.exe" ]; then
        HOST_CC="$HOME/scoop/apps/gcc/current/bin/gcc.exe"
    else
        echo "ERROR: no host C compiler found. Set HOST_CC=/path/to/gcc and re-run." >&2
        exit 1
    fi
fi
echo "HOST_CC=$HOST_CC"

# Ensure make is available (alpine vitasdk image may not ship it).
if ! command -v make >/dev/null 2>&1; then
    if command -v apk >/dev/null 2>&1; then
        apk add --no-cache make >/dev/null
    fi
fi

echo "Building FFmpeg $FFMPEG_REF for arm-vita-eabi..."

if [ ! -d "$FFMPEG_SRC" ]; then
    git clone --depth 1 --branch "$FFMPEG_REF" \
        https://github.com/FFmpeg/FFmpeg.git "$FFMPEG_SRC"
fi

# Out-of-tree build dir; clean prior attempts so configure re-runs cleanly.
FFMPEG_OBJ="$FFMPEG_SRC/build-vita"
rm -rf "$FFMPEG_OBJ"
mkdir -p "$FFMPEG_OBJ"
cd "$FFMPEG_OBJ"

CROSS_PREFIX="arm-vita-eabi-"
SYSROOT="$VITASDK/arm-vita-eabi"

# vita-elf-create rejects PIE relocs; force non-PIC. The cortex-a9 + NEON
# settings match what the rest of the Vita game compiles with (see
# vita.toolchain.cmake), but we disable NEON detection inside ffmpeg's
# configure because it occasionally mis-probes on this cross target.
# GCC 15's stricter incompatible-pointer-types diagnostic trips on ffmpeg
# n7.0.2's libavutil/opt.c (enum AVPixelFormat * vs int *). Downgrade to
# warning so the build completes; the underlying enum-vs-int round-trip is
# benign on a 32-bit ARM ABI.
CFLAGS="-fno-pic -fno-pie -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard \
    -ffunction-sections -fdata-sections -O2 \
    -Wno-error=incompatible-pointer-types \
    -Wno-error=int-conversion \
    -isystem $SYSROOT/include"

../configure \
    --prefix="$FFMPEG_BUILD" \
    --target-os=none \
    --arch=arm \
    --cpu=cortex-a9 \
    --enable-cross-compile \
    --cross-prefix="$CROSS_PREFIX" \
    --sysroot="$SYSROOT" \
    --host-cc="$HOST_CC" \
    --cc="${CROSS_PREFIX}gcc" \
    --cxx="${CROSS_PREFIX}g++" \
    --ar="${CROSS_PREFIX}ar" \
    --as="${CROSS_PREFIX}as" \
    --nm="${CROSS_PREFIX}nm" \
    --ranlib="${CROSS_PREFIX}ranlib" \
    --strip="${CROSS_PREFIX}strip" \
    --extra-cflags="$CFLAGS" \
    --extra-cxxflags="$CFLAGS" \
    --pkg-config-flags="--static" \
    --enable-static \
    --disable-shared \
    --disable-pic \
    --disable-neon \
    --disable-asm \
    --disable-everything \
    --disable-autodetect \
    --disable-programs \
    --disable-doc \
    --disable-htmlpages \
    --disable-manpages \
    --disable-podpages \
    --disable-txtpages \
    --disable-debug \
    --disable-network \
    --disable-avdevice \
    --disable-avfilter \
    --disable-postproc \
    --disable-iconv \
    --disable-zlib \
    --disable-bzlib \
    --disable-lzma \
    --disable-xlib \
    --disable-sdl2 \
    --disable-jni \
    --disable-mediacodec \
    --disable-vulkan \
    --enable-avcodec \
    --enable-avformat \
    --enable-avutil \
    --enable-swresample \
    --enable-decoder=adpcm_adx \
    --enable-parser=adx \
    --enable-protocol=file

make -j"$BUILD_JOBS"
make install

echo
echo "FFmpeg (vita) installed to $FFMPEG_BUILD"

# Verify the adpcm_adx decoder symbol is present in libavcodec.a.
# Buffer nm output so `grep -q`'s early exit doesn't SIGPIPE nm under
# `set -o pipefail` (busybox-grep + Alpine CI surfaces this).
nm_dump="$("${CROSS_PREFIX}nm" "$FFMPEG_BUILD/lib/libavcodec.a" 2>/dev/null)"
if echo "$nm_dump" | grep -q "adpcm_adx_decoder"; then
    echo "  Verified: adpcm_adx_decoder symbol present in libavcodec.a"
else
    echo "  ERROR: adpcm_adx_decoder symbol NOT found in libavcodec.a" >&2
    echo "$nm_dump" | grep -i "adpcm" | head -20 >&2 || true
    exit 1
fi

echo "Done."
