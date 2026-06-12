#!/usr/bin/env bash
# Build SDL3_net + GekkoNet for PlayStation Vita using vitasdk.
#
# Designed to run inside the vitasdk/vitasdk:latest docker image (or any host
# where $VITASDK points at a vitasdk install).
#
# Outputs:
#   third_party/SDL_net/build-vita/{lib/libSDL3_net.a,include/SDL3_net/SDL_net.h}
#   third_party/GekkoNet/build-vita/{lib/libGekkoNet.a,include/*.h}
#
# Notes on SDL3_net patch: upstream pulls in <net/if.h> and <ifaddrs.h> on
# every non-Windows target. vitasdk newlib has neither, so we comment out the
# include and stub the interface-enumeration helpers. 3SX uses explicit-IP
# matchmaking only, so the stub is functionally correct.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
THIRD_PARTY="$ROOT_DIR/third_party"

mkdir -p "$THIRD_PARTY"

: "${VITASDK:?VITASDK must be set (point at vitasdk install root)}"

BUILD_JOBS="${BUILD_JOBS:-$(nproc 2>/dev/null || echo 4)}"
echo "Using $BUILD_JOBS build job(s)"
echo "VITASDK=$VITASDK"

# -----------------------------
# SDL3_net (Vita)
# -----------------------------

SDL3_NET_REF="92022dc"
SDL3_NET_SRC="$THIRD_PARTY/SDL_net-src"
SDL3_NET_BUILD="$THIRD_PARTY/SDL_net/build-vita"

if [ -d "$SDL3_NET_BUILD" ]; then
    echo "SDL3_net (vita) already built at $SDL3_NET_BUILD"
else
    echo "Building SDL3_net (vita) @ $SDL3_NET_REF..."

    if [ ! -d "$SDL3_NET_SRC" ]; then
        git clone https://github.com/libsdl-org/SDL_net.git "$SDL3_NET_SRC"
    fi
    git -C "$SDL3_NET_SRC" fetch --quiet origin
    git -C "$SDL3_NET_SRC" -c advice.detachedHead=false checkout "$SDL3_NET_REF"

    # Patch: gate <net/if.h> / <ifaddrs.h> + add a no-op Vita platform branch.
    # Idempotent: skip if already applied.
    if ! grep -q "__vita__" "$SDL3_NET_SRC/src/SDL_net.c"; then
        python3 - "$SDL3_NET_SRC/src/SDL_net.c" <<'PY'
import sys, re
path = sys.argv[1]
src = open(path, encoding="utf-8").read()

# 1. Gate <net/if.h>.
src = src.replace(
    "#include <netinet/in.h>\n#include <net/if.h>\n",
    "#include <netinet/in.h>\n#ifndef __vita__\n#include <net/if.h>\n#endif\n",
)

# 2. Gate ifaddrs.h behind a Vita-aware chain.
src = src.replace(
    "#ifdef USE_NETLINK\n#include <linux/netlink.h>\n#include <linux/rtnetlink.h>\n#else\n#include <ifaddrs.h>\n#endif",
    "#if defined(__vita__)\n/* Vita: no interface enumeration; explicit-IP only. */\n#elif defined(USE_NETLINK)\n#include <linux/netlink.h>\n#include <linux/rtnetlink.h>\n#else\n#include <ifaddrs.h>\n#endif",
)

# 3. Add Vita branch with no-op interface helpers.
src = src.replace(
    "#else\n#error implement me for your platform.\n#endif",
    """#elif defined(__vita__)

static bool InitInterfaceChangeNotifications(void) { return true; }
static void QuitInterfaceChangeNotifications(void) {}
static void RefreshInterfaces(void)
{
    SDL_LockRWLockForWriting(interface_rwlock);
    NetworkInterfaces *old_interfaces = interfaces;
    const int old_num_interfaces = num_interfaces;
    interfaces = NULL;
    num_interfaces = 0;
    SDL_UnlockRWLock(interface_rwlock);
    FreeNetworkInterfaces(old_interfaces, old_num_interfaces);
}

#else
#error implement me for your platform.
#endif""",
)

open(path, "w", encoding="utf-8").write(src)
print("Patched", path)
PY
    fi

    rm -rf "$SDL3_NET_SRC/build-vita"
    cmake -S "$SDL3_NET_SRC" -B "$SDL3_NET_SRC/build-vita" \
        -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
        -DCMAKE_PREFIX_PATH="$VITASDK/arm-vita-eabi" \
        -DSDL3_DIR="$VITASDK/arm-vita-eabi/lib/cmake/SDL3" \
        -DCMAKE_INSTALL_PREFIX="$SDL3_NET_BUILD" \
        -DCMAKE_C_FLAGS="-fno-pic -fno-pie" \
        -DBUILD_SHARED_LIBS=OFF \
        -DSDLNET_INSTALL=ON

    cmake --build "$SDL3_NET_SRC/build-vita" -j"$BUILD_JOBS"
    cmake --install "$SDL3_NET_SRC/build-vita"

    echo "SDL3_net (vita) installed to $SDL3_NET_BUILD"
fi

# -----------------------------
# GekkoNet (Vita)
# -----------------------------

GEKKONET_REF="7be848c"
GEKKONET_SRC="$THIRD_PARTY/GekkoNet-src"
GEKKONET_BUILD="$THIRD_PARTY/GekkoNet/build-vita"

if [ -d "$GEKKONET_BUILD" ]; then
    echo "GekkoNet (vita) already built at $GEKKONET_BUILD"
else
    echo "Building GekkoNet (vita) @ $GEKKONET_REF..."

    if [ ! -d "$GEKKONET_SRC" ]; then
        git clone https://github.com/HeatXD/GekkoNet.git "$GEKKONET_SRC"
    fi
    git -C "$GEKKONET_SRC" fetch --quiet origin
    git -C "$GEKKONET_SRC" -c advice.detachedHead=false checkout "$GEKKONET_REF"

    # Patch: drop the unconditional -fPIC from GekkoLib/CMakeLists.txt.
    # Vita's vita-elf-create rejects R_ARM_GOT_BREL / R_ARM_BASE_PREL relocs
    # that PIC C++ produces (vtables, exception handlers). Idempotent.
    if grep -q '\-fPIC' "$GEKKONET_SRC/GekkoLib/CMakeLists.txt" 2>/dev/null; then
        sed -i 's|set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")|# -fPIC stripped for Vita (vita-elf-create rejects GOT-rel relocs)|' \
            "$GEKKONET_SRC/GekkoLib/CMakeLists.txt"
    fi

    rm -rf "$GEKKONET_SRC/build-vita"
    cmake -S "$GEKKONET_SRC" -B "$GEKKONET_SRC/build-vita" \
        -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_FLAGS="-fno-pic -fno-pie" \
        -DCMAKE_CXX_FLAGS="-fno-pic -fno-pie" \
        -DNO_ASIO_BUILD=ON \
        -DBUILD_SHARED_LIBS=OFF

    cmake --build "$GEKKONET_SRC/build-vita" -j"$BUILD_JOBS"

    mkdir -p "$GEKKONET_BUILD/include" "$GEKKONET_BUILD/lib"
    cp -r "$GEKKONET_SRC/GekkoLib/include/." "$GEKKONET_BUILD/include/"
    find "$GEKKONET_SRC/build-vita" -name "*.a" -exec cp {} "$GEKKONET_BUILD/lib/libGekkoNet.a" \;

    echo "GekkoNet (vita) installed to $GEKKONET_BUILD"
fi

# -----------------------------
# vitaGL + vitaShaRK with HAVE_VITA3K_SUPPORT=1
# The vitasdk prebuilt vitaGL calls sceGxmVshInitialize, which Vita3K rejects.
# Building from source with HAVE_VITA3K_SUPPORT=1 swaps in the regular
# sceGxmInitialize path so the same VPK boots on hardware AND Vita3K.
# CMakeLists links third_party/vitaGL-vita3k/lib/libvitaGL.a directly.
# -----------------------------

VITAGL_OUT="$THIRD_PARTY/vitaGL-vita3k"
VITAGL_SRC="$THIRD_PARTY/vitaGL-src"
VITASHARK_SRC="$THIRD_PARTY/vitaShaRK-src"

if [ -f "$VITAGL_OUT/lib/libvitaGL.a" ] && [ -f "$VITAGL_OUT/lib/libvitashark.a" ]; then
    echo "vitaGL-vita3k already built at $VITAGL_OUT"
else
    echo "Building vitaShaRK..."
    if [ ! -d "$VITASHARK_SRC" ]; then
        git clone --depth 1 https://github.com/Rinnegatamante/vitaShaRK.git "$VITASHARK_SRC"
    fi
    make -C "$VITASHARK_SRC" -j"$BUILD_JOBS"
    # Install into the SDK sysroot so vitaGL picks up the fresh vitashark.h
    # (the vitasdk prebuilt header predates shark_init_simple).
    make -C "$VITASHARK_SRC" install

    echo "Building vitaGL (HAVE_VITA3K_SUPPORT=1)..."
    if [ ! -d "$VITAGL_SRC" ]; then
        git clone --depth 1 https://github.com/Rinnegatamante/vitaGL.git "$VITAGL_SRC"
    fi
    make -C "$VITAGL_SRC" HAVE_VITA3K_SUPPORT=1 -j"$BUILD_JOBS"

    mkdir -p "$VITAGL_OUT/lib" "$VITAGL_OUT/include"
    find "$VITAGL_SRC" -name 'libvitaGL.a' -exec cp {} "$VITAGL_OUT/lib/libvitaGL.a" \;
    find "$VITASHARK_SRC" -name 'libvitashark.a' -exec cp {} "$VITAGL_OUT/lib/libvitashark.a" \;
    find "$VITAGL_SRC" -maxdepth 2 -name 'vitaGL.h' -exec cp {} "$VITAGL_OUT/include/vitaGL.h" \;
    find "$VITASHARK_SRC" -maxdepth 2 -name 'vitashark.h' -exec cp {} "$VITAGL_OUT/include/vitashark.h" \;

    [ -f "$VITAGL_OUT/lib/libvitaGL.a" ] || { echo "vitaGL build produced no libvitaGL.a" >&2; exit 1; }
    [ -f "$VITAGL_OUT/lib/libvitashark.a" ] || { echo "vitaShaRK build produced no libvitashark.a" >&2; exit 1; }
    echo "vitaGL-vita3k installed to $VITAGL_OUT"
fi

# -----------------------------
# minizip-ng (Vita)
# vitasdk ships old minizip with bz2/lzma/ppmd/zstd/openssl deps wired in,
# which drag in symbols we don't need (and ppmd isn't available). Build the
# minizip-ng we use on Win/Linux with all optional codecs OFF.
# -----------------------------

MINIZIP_NG_TAG="4.1.0"
MINIZIP_NG_SRC="$THIRD_PARTY/minizip-ng-src"
MINIZIP_NG_BUILD="$THIRD_PARTY/minizip-ng/build-vita"

if [ -d "$MINIZIP_NG_BUILD" ]; then
    echo "minizip-ng (vita) already built at $MINIZIP_NG_BUILD"
else
    echo "Building minizip-ng (vita) @ $MINIZIP_NG_TAG..."

    if [ ! -d "$MINIZIP_NG_SRC" ]; then
        git clone --branch "$MINIZIP_NG_TAG" --single-branch \
            https://github.com/zlib-ng/minizip-ng "$MINIZIP_NG_SRC"
    fi

    rm -rf "$MINIZIP_NG_SRC/build-vita"
    cmake -S "$MINIZIP_NG_SRC" -B "$MINIZIP_NG_SRC/build-vita" \
        -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
        -DCMAKE_C_FLAGS="-fno-pic -fno-pie" \
        -DCMAKE_INSTALL_PREFIX="$MINIZIP_NG_BUILD" \
        -DMZ_COMPAT=OFF \
        -DMZ_ZLIB_FLAVOR=zlib \
        -DMZ_BZIP2=OFF \
        -DMZ_LZMA=OFF \
        -DMZ_PPMD=OFF \
        -DMZ_ZSTD=OFF \
        -DMZ_LIBCOMP=OFF \
        -DMZ_PKCRYPT=OFF \
        -DMZ_WZAES=OFF \
        -DMZ_OPENSSL=OFF \
        -DMZ_LIBBSD=OFF \
        -DMZ_DECOMPRESS_ONLY=ON

    cmake --build "$MINIZIP_NG_SRC/build-vita" -j"$BUILD_JOBS"
    cmake --install "$MINIZIP_NG_SRC/build-vita"

    echo "minizip-ng (vita) installed to $MINIZIP_NG_BUILD"
fi

echo "All Vita dependencies installed in $THIRD_PARTY"
