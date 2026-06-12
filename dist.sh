#!/usr/bin/env bash
# dist.sh — standalone, repo-only packager.
#
# Builds the requested platform via build.sh, then assembles a ready-to-share
# zip under dist/. No dependence on the fistbump-server repo.
#
# Usage:
#   bash dist.sh <pc|android|vita> [VERSION]
#
# Optional env:
#   DISCORD_APP_ID   bake into the build (forwarded to build.sh)
#   REGIONS_FILE     path to a regions.txt to bundle with PC and Android
#   INCLUDE_LAUNCHER path to a 3sx_launcher_online.exe to bundle with the PC zip
#   BUILD_VERSION    override the version embedded in 3sx via cmake
set -euo pipefail

TARGET="${1:-}"
VERSION="${2:-${BUILD_VERSION:-dev}}"
HERE="$(cd "$(dirname "$0")" && pwd)"
DIST="$HERE/dist"

case "$TARGET" in
    pc|android|vita) ;;
    "")
        echo "usage: $0 <pc|android|vita> [VERSION]" >&2
        exit 1 ;;
    *)
        echo "unknown target: $TARGET (use pc|android|vita)" >&2
        exit 1 ;;
esac

mkdir -p "$DIST"

# -------- Build --------
echo "==> dist.sh: building $TARGET (version=$VERSION)"
# Forward release-relevant env vars to build.sh. Without `export` the child
# bash inherits a clean env so DISCORD_APP_ID + ANDROID_ABI etc. would be
# silently dropped.
export BUILD_VERSION="$VERSION"
[ -n "${DISCORD_APP_ID:-}" ]   && export DISCORD_APP_ID
[ -n "${BUILD_GIT_SHA:-}" ]    && export BUILD_GIT_SHA
[ -n "${ANDROID_ABI:-}" ]      && export ANDROID_ABI
[ -n "${ANDROID_API:-}" ]      && export ANDROID_API
[ -n "${ANDROID_NDK_HOME:-}" ] && export ANDROID_NDK_HOME
[ -n "${ANDROID_HOME:-}" ]     && export ANDROID_HOME
[ -n "${JAVA_HOME:-}" ]        && export JAVA_HOME
[ -n "${JOBS:-}" ]             && export JOBS
bash "$HERE/build.sh" "$TARGET"

# -------- Stage layout --------
STAGE="$DIST/3sx-$VERSION-$TARGET"
rm -rf "$STAGE"
mkdir -p "$STAGE"

stage_regions() {
    if [ -n "${REGIONS_FILE:-}" ] && [ -f "$REGIONS_FILE" ]; then
        cp -v "$REGIONS_FILE" "$STAGE/regions.txt"
    elif [ -n "${REGIONS_FILE:-}" ]; then
        echo "WARN: REGIONS_FILE=$REGIONS_FILE does not exist — packaging without regions.txt" >&2
    else
        echo "NOTE: REGIONS_FILE not set — packaging without a baked regions.txt." >&2
        echo "      Run 'bash setup-regions.sh' to generate one interactively." >&2
    fi
}

case "$TARGET" in
    pc)
        # 1. exe + shaders. The VERSION marker is what the launcher compares
        #    against GitHub's latest release tag — without it a flat install
        #    can't tell what version it is and re-prompts for updates forever.
        cp -v "$HERE/build/application/bin/3sx.exe" "$STAGE/3sx.exe"
        printf '%s' "$VERSION" > "$STAGE/VERSION"
        if [ -d "$HERE/build/application/bin/shaders" ]; then
            cp -rv "$HERE/build/application/bin/shaders" "$STAGE/shaders"
        fi
        # CMake's install step also drops SDL3.dll + ffmpeg DLLs into
        # build/application/bin/ because they live OUTSIDE /mingw64/bin (SDL3
        # is built from source). Seed those before walking the closure so the
        # objdump search has them as candidates.
        for f in "$HERE/build/application/bin/"*.dll; do
            [ -f "$f" ] && cp -n "$f" "$STAGE/"
        done

        # 2. Optional launcher.
        if [ -n "${INCLUDE_LAUNCHER:-}" ] && [ -f "$INCLUDE_LAUNCHER" ]; then
            cp -v "$INCLUDE_LAUNCHER" "$STAGE/3sx_launcher_online.exe"
        fi

        # 3. DLL closure via objdump — transitive set of deps reachable from
        #    3sx.exe + any launcher exe. We probe THREE sources in order:
        #    (a) already-staged DLLs (build/application/bin/ leftovers);
        #    (b) /mingw64/bin/ (the bulk of FFmpeg / SDL3 deps);
        #    (c) the original build/application/bin/ for SDL3.dll itself.
        #    System DLLs (KERNEL32 etc.) live in C:\Windows\System32 and are
        #    skipped because we don't ship Windows itself.
        MINGW="${MINGW_PREFIX:-/mingw64}"
        if [ ! -d "$MINGW/bin" ]; then
            MINGW="/c/msys64/mingw64"
        fi
        BUILD_BIN="$HERE/build/application/bin"
        if [ ! -d "$MINGW/bin" ]; then
            echo "WARN: no mingw64 bin dir found; PC zip may be missing DLLs" >&2
        fi
        echo "==> dist.sh: resolving DLL closure (mingw=$MINGW build_bin=$BUILD_BIN)"
        declare -A SEEN
        queue=("3sx.exe")
        [ -f "$STAGE/3sx_launcher_online.exe" ] && queue+=("3sx_launcher_online.exe")
        while [ "${#queue[@]}" -gt 0 ]; do
            cur="${queue[0]}"
            queue=("${queue[@]:1}")
            lc="$(echo "$cur" | tr 'A-Z' 'a-z')"
            [ "${SEEN[$lc]:-}" = "1" ] && continue
            SEEN[$lc]=1
            src=""
            if [ -f "$STAGE/$cur" ]; then
                src="$STAGE/$cur"
            elif [ -f "$MINGW/bin/$cur" ]; then
                src="$MINGW/bin/$cur"
                cp -n "$src" "$STAGE/$cur"
            elif [ -f "$BUILD_BIN/$cur" ]; then
                src="$BUILD_BIN/$cur"
                cp -n "$src" "$STAGE/$cur"
            else
                continue
            fi
            while read -r dep; do
                [ -n "$dep" ] && queue+=("$dep")
            done < <(objdump -p "$src" 2>/dev/null | awk '/DLL Name:/ {print $3}')
        done
        echo "==> dist.sh: ${#SEEN[@]} DLLs in closure"

        # Prune anything we seeded but is not in the closure (e.g. the cmake
        # install step copies some bonus DLLs we don't actually need).
        shopt -s nocasematch
        for f in "$STAGE"/*.dll; do
            [ -f "$f" ] || continue
            bn="$(basename "$f" | tr 'A-Z' 'a-z')"
            if [ "${SEEN[$bn]:-}" != "1" ]; then
                rm -f "$f"
            fi
        done
        shopt -u nocasematch

        stage_regions
        ;;

    android)
        APK="$HERE/android-project/app/build/outputs/apk/debug/app-debug.apk"
        [ -f "$APK" ] || { echo "APK not found at $APK" >&2; exit 1; }
        cp -v "$APK" "$STAGE/3sx-$VERSION-debug.apk"

        # If an install-and-run.bat is present in the project, include a copy
        # so sideloading is one click. Shaders/AFS staging stays the user's job.
        if [ -f "$HERE/android-project/install-and-run.bat" ]; then
            cp -v "$HERE/android-project/install-and-run.bat" "$STAGE/install-and-run.bat"
        fi
        stage_regions
        ;;

    vita)
        VPK="$HERE/build-vita/3sx.vpk"
        [ -f "$VPK" ] || { echo "VPK not found at $VPK" >&2; exit 1; }
        cp -v "$VPK" "$STAGE/3sx-$VERSION.vpk"
        # On Vita the user pre-stages everything under ux0:data/3sx/ so regions
        # are dropped via the file manager, not bundled in the VPK.
        ;;
esac

# -------- Zip --------
ZIP="$DIST/3sx-$VERSION-$TARGET.zip"
rm -f "$ZIP"
echo "==> dist.sh: zipping → $ZIP"
if command -v zip >/dev/null 2>&1; then
    (cd "$STAGE/.." && zip -qr "$ZIP" "$(basename "$STAGE")")
else
    # Pure-Python fallback so the script works in a vanilla MSYS2 without zip.
    python -c "
import os, sys, zipfile
root = sys.argv[1]
out = sys.argv[2]
parent = os.path.dirname(root)
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for cwd, _, files in os.walk(root):
        for f in files:
            full = os.path.join(cwd, f)
            arc = os.path.relpath(full, parent)
            z.write(full, arc)
" "$STAGE" "$ZIP"
fi

size_mb=$(du -m "$ZIP" | awk '{print $1}')
echo "==> dist.sh: done"
echo "    stage: $STAGE"
echo "    zip:   $ZIP ($size_mb MB)"
if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$ZIP"
fi
