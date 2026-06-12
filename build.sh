#!/usr/bin/env bash
# build.sh — orchestrates a clean build for a single platform.
#
# Usage:  bash build.sh <target>
#   target ::= pc | android | vita
#
# Requires:
#   pc       MSYS2 MinGW64 (Windows), clang + cmake (Linux/macOS)
#   android  JDK 17 + Android NDK 26.1 (ANDROID_NDK_HOME env)
#   vita     Docker (uses vitasdk/vitasdk image — no host toolchain needed)
#
# Each target produces one artifact:
#   pc       build/application/bin/3sx[.exe]
#   android  android-project/app/build/outputs/apk/debug/app-debug.apk
#   vita     build-vita/3sx.vpk
#
# Optional env:
#   DISCORD_APP_ID  bake a Discord application id into the build for RPC
#   JOBS            parallelism (defaults to $(nproc))
set -euo pipefail

TARGET="${1:-}"
JOBS="${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)}"
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"

case "$TARGET" in
    pc)
        echo "==> building PC ($JOBS jobs)"
        [ -d third_party/SDL_net/build ] || bash build-deps.sh
        CMAKE_ARGS=(-B build -DCMAKE_BUILD_TYPE=Release -G Ninja)
        if [ -n "${DISCORD_APP_ID:-}" ]; then
            CMAKE_ARGS+=(-DDISCORD_APP_ID="${DISCORD_APP_ID}")
        fi
        CC=clang CXX=clang++ cmake "${CMAKE_ARGS[@]}"
        cmake --build build --target 3sx -j "$JOBS"
        cmake --install build --prefix build/application >/dev/null
        echo "==> done: $(ls -la build/application/bin/3sx* 2>/dev/null | head -1)"
        ;;
    android)
        echo "==> building Android ($JOBS jobs)"
        : "${ANDROID_NDK_HOME:?ANDROID_NDK_HOME env required (path to NDK r26b install)}"
        [ -d third_party/SDL_net/build-android ] || bash build-deps-android.sh

        TOOLCHAIN="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake"
        if [ ! -f "$TOOLCHAIN" ]; then
            echo "Android toolchain.cmake missing at $TOOLCHAIN" >&2
            exit 1
        fi

        ABI="${ANDROID_ABI:-arm64-v8a}"
        API="${ANDROID_API:-29}"
        CMAKE_ARGS=(
            -B build-android -G Ninja
            -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN"
            -DANDROID_ABI="$ABI"
            -DANDROID_PLATFORM="android-$API"
            -DCMAKE_BUILD_TYPE=Release
            -DANDROID_STL=c++_shared
        )
        if [ -n "${DISCORD_APP_ID:-}" ]; then
            CMAKE_ARGS+=(-DDISCORD_APP_ID="${DISCORD_APP_ID}")
        fi

        cmake "${CMAKE_ARGS[@]}"
        cmake --build build-android --target 3sx -j "$JOBS"

        JNI="android-project/app/src/main/jniLibs/$ABI"
        mkdir -p "$JNI"
        cp -v build-android/lib3sx.so "$JNI/"
        cp -v third_party/sdl3/build-android/lib/libSDL3.so "$JNI/"
        # Optional ffmpeg .so set — present only when SOUND_ENABLED for Android.
        for so in third_party/ffmpeg/build-android/lib/libav*.so \
                  third_party/ffmpeg/build-android/lib/libsw*.so; do
            [ -f "$so" ] && cp -v "$so" "$JNI/"
        done
        # NDK libc++ shared runtime — pre-built sysroot ships it next to clang.
        LIBCXX=$(find "$ANDROID_NDK_HOME/toolchains/llvm/prebuilt" \
            -path "*/aarch64-linux-android/libc++_shared.so" 2>/dev/null | head -1)
        [ -n "$LIBCXX" ] && cp -v "$LIBCXX" "$JNI/"

        cd android-project
        chmod +x ./gradlew
        ./gradlew assembleDebug --no-daemon
        echo "==> done: $(ls -la app/build/outputs/apk/debug/*.apk 2>/dev/null | head -1)"
        ;;
    vita)
        echo "==> building Vita via docker ($JOBS jobs)"
        if ! command -v docker >/dev/null 2>&1; then
            echo "docker required for Vita builds" >&2
            exit 1
        fi
        WIN_PATH="$(pwd -W 2>/dev/null || pwd)"
        docker run --rm -v "${WIN_PATH}:/workspace" -w /workspace vitasdk/vitasdk:latest sh -euxc "
            apk add --no-cache bash cmake ninja python3 make >/dev/null
            bash build-deps-vita.sh
            if [ ! -d third_party/ffmpeg/build-vita-arm ]; then bash build-ffmpeg-vita.sh; fi
            cmake -B build-vita \
                -DCMAKE_TOOLCHAIN_FILE=\$VITASDK/share/vita.toolchain.cmake \
                -DCMAKE_BUILD_TYPE=Release \
                -G Ninja
            cmake --build build-vita --target 3sx.vpk -j ${JOBS}
        "
        echo "==> done: build-vita/3sx.vpk"
        ;;
    "")
        echo "usage: $0 <pc|android|vita>" >&2
        exit 1
        ;;
    *)
        echo "unknown target: $TARGET" >&2
        echo "usage: $0 <pc|android|vita>" >&2
        exit 1
        ;;
esac
