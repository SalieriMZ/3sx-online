#!/usr/bin/env bash
# Cross-compile 3SX third_party deps for Android arm64-v8a (API 29+).
# Output: third_party/<dep>/build-android/{include,lib}/...
# Required: NDK 26.1 installed; ANDROID_NDK_HOME or ANDROID_HOME pointing at it.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
THIRD_PARTY="$ROOT_DIR/third_party"
mkdir -p "$THIRD_PARTY"

ABI="${ABI:-arm64-v8a}"
API="${API:-29}"

NDK="${ANDROID_NDK_HOME:-}"
if [ -z "$NDK" ] && [ -n "${ANDROID_HOME:-}" ]; then
    # scoop layout: ANDROID_HOME/ndk/<version>
    NDK=$(find "$ANDROID_HOME/ndk" -maxdepth 1 -mindepth 1 -type d 2>/dev/null | sort | tail -1)
fi
if [ -z "$NDK" ] || [ ! -f "$NDK/build/cmake/android.toolchain.cmake" ]; then
    echo "ERROR: Could not find NDK. Set ANDROID_NDK_HOME or install via sdkmanager." >&2
    exit 1
fi
echo "Using NDK: $NDK"

TOOLCHAIN="$NDK/build/cmake/android.toolchain.cmake"
BUILD_JOBS="${BUILD_JOBS:-4}"

# Common CMake args for all deps.
# Force Ninja so CMake doesn't fall through to the default Visual Studio
# generator on Windows (which can't drive the NDK clang toolchain).
android_cmake_args=(
    -G Ninja
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN"
    -DANDROID_ABI="$ABI"
    -DANDROID_PLATFORM="android-$API"
    -DCMAKE_BUILD_TYPE=Release
    -DANDROID_STL=c++_shared
)

# -----------------------------
# SDL3 (shared lib → libSDL3.so)
# -----------------------------
SDL_REF="3.4.8"
SDL_DIR="$THIRD_PARTY/sdl3"
SDL_BUILD="$SDL_DIR/build-android"
SDL_SRC="$SDL_DIR/src"

if [ -d "$SDL_BUILD" ]; then
    echo "SDL3 already built for Android at $SDL_BUILD"
else
    echo "Building SDL3 for Android..."
    if [ ! -d "$SDL_SRC" ]; then
        git clone --depth=1 --branch "release-$SDL_REF" \
            https://github.com/libsdl-org/SDL.git "$SDL_SRC" || \
        git clone https://github.com/libsdl-org/SDL.git "$SDL_SRC"
    fi

    # Apply 3SX-specific Mali GPU patches (see android-patches/sdl3-mali-patches.patch).
    # Three fixes: compositeAlpha fallback chain (Mali doesn't support OPAQUE),
    # SUBOPTIMAL not treated as recreate on Android (orientation transform mismatch
    # caused 20 FPS recreate loop), shaderClipDistance gated on availability.
    # Idempotent: git apply --check exits non-zero if already applied, so we skip.
    PATCH="$ROOT_DIR/android-patches/sdl3-mali-patches.patch"
    if [ -f "$PATCH" ]; then
        if git -C "$SDL_SRC" apply --check "$PATCH" 2>/dev/null; then
            echo "Applying SDL3 Mali patches..."
            git -C "$SDL_SRC" apply "$PATCH"
        else
            echo "SDL3 Mali patches already applied (or conflict — review manually)"
        fi
    fi

    cmake -S "$SDL_SRC" -B "$SDL_SRC/cmake-build-android" \
        "${android_cmake_args[@]}" \
        -DCMAKE_INSTALL_PREFIX="$SDL_BUILD" \
        -DSDL_SHARED=ON -DSDL_STATIC=OFF

    cmake --build "$SDL_SRC/cmake-build-android" -j"$BUILD_JOBS"
    cmake --install "$SDL_SRC/cmake-build-android"

    # Stash SDL3's Java sources for ThreeSXActivity to subclass.
    mkdir -p "$ROOT_DIR/android-project/app/src/main/java/org/libsdl/app"
    cp -v "$SDL_SRC/android-project/app/src/main/java/org/libsdl/app/"*.java \
        "$ROOT_DIR/android-project/app/src/main/java/org/libsdl/app/" || true

    echo "SDL3 Android build → $SDL_BUILD"
fi

# -----------------------------
# GekkoNet (static)
# -----------------------------
GEKKONET_REF="7be848c"
GEKKONET_DIR="$THIRD_PARTY/GekkoNet"
GEKKONET_BUILD="$GEKKONET_DIR/build-android"

if [ -d "$GEKKONET_BUILD" ]; then
    echo "GekkoNet already built for Android at $GEKKONET_BUILD"
else
    echo "Building GekkoNet for Android..."
    GEKKONET_SRC=$(mktemp -d)
    git clone https://github.com/HeatXD/GekkoNet.git "$GEKKONET_SRC"
    git -C "$GEKKONET_SRC" -c advice.detachedHead=false checkout "$GEKKONET_REF"

    cmake -S "$GEKKONET_SRC" -B "$GEKKONET_SRC/cmake-build" \
        "${android_cmake_args[@]}" \
        -DNO_ASIO_BUILD=ON \
        -DBUILD_SHARED_LIBS=OFF

    cmake --build "$GEKKONET_SRC/cmake-build" -j"$BUILD_JOBS"

    mkdir -p "$GEKKONET_BUILD/include" "$GEKKONET_BUILD/lib"
    cp -r "$GEKKONET_SRC/GekkoLib/include/." "$GEKKONET_BUILD/include/"
    find "$GEKKONET_SRC" -name "*.a" -exec cp {} "$GEKKONET_BUILD/lib/libGekkoNet.a" \;

    rm -rf "$GEKKONET_SRC"
    echo "GekkoNet Android build → $GEKKONET_BUILD"
fi

# -----------------------------
# SDL_net (static)
# -----------------------------
SDL3_NET_REF="92022dc"
SDL3_NET_DIR="$THIRD_PARTY/SDL_net"
SDL3_NET_BUILD="$SDL3_NET_DIR/build-android"

if [ -d "$SDL3_NET_BUILD" ]; then
    echo "SDL_net already built for Android at $SDL3_NET_BUILD"
else
    echo "Building SDL_net for Android..."
    SDL3_NET_SRC=$(mktemp -d)
    git clone https://github.com/libsdl-org/SDL_net.git "$SDL3_NET_SRC"
    git -C "$SDL3_NET_SRC" -c advice.detachedHead=false checkout "$SDL3_NET_REF"

    cmake -S "$SDL3_NET_SRC" -B "$SDL3_NET_SRC/cmake-build" \
        "${android_cmake_args[@]}" \
        -DCMAKE_INSTALL_PREFIX="$SDL3_NET_BUILD" \
        -DSDL3_DIR="$SDL_BUILD/lib/cmake/SDL3" \
        -DBUILD_SHARED_LIBS=OFF \
        -DSDLNET_INSTALL=ON

    cmake --build "$SDL3_NET_SRC/cmake-build" -j"$BUILD_JOBS"
    cmake --install "$SDL3_NET_SRC/cmake-build"

    rm -rf "$SDL3_NET_SRC"
    echo "SDL_net Android build → $SDL3_NET_BUILD"
fi

# -----------------------------
# minizip-ng (static)
# -----------------------------
MINIZIP_REF="4.1.0"
MINIZIP_DIR="$THIRD_PARTY/minizip-ng"
MINIZIP_BUILD="$MINIZIP_DIR/build-android"

if [ -d "$MINIZIP_BUILD" ]; then
    echo "minizip-ng already built for Android at $MINIZIP_BUILD"
else
    echo "Building minizip-ng for Android..."
    MINIZIP_SRC=$(mktemp -d)
    git clone --depth=1 --branch "$MINIZIP_REF" \
        https://github.com/zlib-ng/minizip-ng.git "$MINIZIP_SRC"

    cmake -S "$MINIZIP_SRC" -B "$MINIZIP_SRC/cmake-build" \
        "${android_cmake_args[@]}" \
        -DCMAKE_INSTALL_PREFIX="$MINIZIP_BUILD" \
        -DBUILD_SHARED_LIBS=OFF \
        -DMZ_FETCH_LIBS=ON \
        -DMZ_BZIP2=OFF -DMZ_LZMA=OFF -DMZ_ZSTD=OFF \
        -DMZ_PKCRYPT=OFF -DMZ_WZAES=OFF -DMZ_LIBCOMP=OFF \
        -DMZ_OPENSSL=OFF -DMZ_LIBBSD=OFF

    cmake --build "$MINIZIP_SRC/cmake-build" -j"$BUILD_JOBS"
    cmake --install "$MINIZIP_SRC/cmake-build"

    # Source uses <minizip-ng/...> include paths (matches the upstream mingw
    # package layout). Newer minizip-ng installs to include/minizip/ by
    # default. Symlink-rename so include paths resolve.
    if [ -d "$MINIZIP_BUILD/include/minizip" ] && [ ! -d "$MINIZIP_BUILD/include/minizip-ng" ]; then
        mv "$MINIZIP_BUILD/include/minizip" "$MINIZIP_BUILD/include/minizip-ng"
    fi

    rm -rf "$MINIZIP_SRC"
    echo "minizip-ng Android build → $MINIZIP_BUILD"
fi

# -----------------------------
# ffmpeg (ADPCM-ADX decoder + swresample, shared)
# -----------------------------
# 3SX music is Capcom-format ADX streams. adx.c calls
# avcodec_find_decoder(AV_CODEC_ID_ADPCM_ADX) + swr_convert. We don't need
# demuxers (AFS handles container) or any other codecs/filters. Build the
# smallest ffmpeg that satisfies adx.c.
FFMPEG_REF="n7.0.2"
FFMPEG_DIR="$THIRD_PARTY/ffmpeg"
FFMPEG_BUILD="$FFMPEG_DIR/build-android"
FFMPEG_SRC="$FFMPEG_DIR/src"

if [ -d "$FFMPEG_BUILD" ]; then
    echo "ffmpeg already built for Android at $FFMPEG_BUILD"
else
    echo "Building ffmpeg for Android..."
    if [ ! -d "$FFMPEG_SRC" ]; then
        git clone --depth=1 --branch "$FFMPEG_REF" \
            https://github.com/FFmpeg/FFmpeg.git "$FFMPEG_SRC"
    fi

    NDK_HOST_TAG="windows-x86_64"
    if [ "$(uname -s)" = "Darwin" ]; then NDK_HOST_TAG="darwin-x86_64"; fi
    if [ "$(uname -s)" = "Linux"  ]; then NDK_HOST_TAG="linux-x86_64";  fi
    TOOL="$NDK/toolchains/llvm/prebuilt/$NDK_HOST_TAG/bin"

    # ffmpeg's configure compiles a tiny host-side bootstrap to autodetect
    # toolchain caps. msys2's `cc` is just a shim; point host-cc at the real
    # mingw gcc so the C11 check passes. Cross-compiled artefacts still use
    # NDK clang via --cc.
    # Locate gcc explicitly. scoop's gcc lives at ~/scoop/apps/gcc/current/bin
    # and isn't always in inherited PATH for background bash spawns.
    if [ -z "${HOST_CC:-}" ]; then
        if command -v gcc >/dev/null 2>&1; then
            HOST_CC="$(command -v gcc)"
        elif [ -x "$HOME/scoop/apps/gcc/current/bin/gcc.exe" ]; then
            HOST_CC="$HOME/scoop/apps/gcc/current/bin/gcc.exe"
            export PATH="$HOME/scoop/apps/gcc/current/bin:$PATH"
        else
            echo "ERROR: gcc not found on PATH. Install via 'scoop install gcc' (sets ~/scoop/apps/gcc)." >&2
            exit 1
        fi
    fi
    echo "ffmpeg host-cc: $HOST_CC"
    (cd "$FFMPEG_SRC" && \
        ./configure \
            --prefix="$FFMPEG_BUILD" \
            --target-os=android \
            --arch=aarch64 \
            --cpu=armv8-a \
            --enable-cross-compile \
            --cross-prefix="$TOOL/llvm-" \
            --cc="$TOOL/aarch64-linux-android${API}-clang" \
            --cxx="$TOOL/aarch64-linux-android${API}-clang++" \
            --host-cc="$HOST_CC" \
            --strip="$TOOL/llvm-strip" \
            --ranlib="$TOOL/llvm-ranlib" \
            --ar="$TOOL/llvm-ar" \
            --nm="$TOOL/llvm-nm" \
            --enable-shared \
            --disable-static \
            --disable-programs \
            --disable-doc \
            --disable-htmlpages \
            --disable-manpages \
            --disable-podpages \
            --disable-txtpages \
            --disable-everything \
            --enable-decoder=adpcm_adx \
            --enable-parser=adx \
            --enable-protocol=file \
            --enable-swresample \
            --enable-pic \
            --disable-avdevice \
            --disable-avfilter \
            --disable-postproc \
            --disable-network \
            --disable-debug \
            --disable-iconv \
            --disable-zlib \
            --disable-bzlib \
            --disable-lzma \
            --disable-xlib \
            --disable-sdl2 \
            --disable-jni \
            --disable-mediacodec \
            --disable-vulkan)

    make -C "$FFMPEG_SRC" -j"$BUILD_JOBS"
    make -C "$FFMPEG_SRC" install
    echo "ffmpeg Android build → $FFMPEG_BUILD"
fi

echo ""
echo "=== Android deps ready ==="
echo "SDL3:      $SDL_BUILD"
echo "GekkoNet:  $GEKKONET_BUILD"
echo "SDL_net:   $SDL3_NET_BUILD"
echo "minizip-ng:$MINIZIP_BUILD"
echo "ffmpeg:    $FFMPEG_BUILD"
echo ""
echo "Next: cd android-project && ./gradlew assembleRelease"
