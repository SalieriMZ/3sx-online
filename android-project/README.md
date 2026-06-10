# 3SX Android port (scaffolding — v1 work-in-progress)

This directory holds the Android-specific glue. The C/C++ game lives under
`../src/` and is built by the root `CMakeLists.txt`.

## Status

Source tree is Android-ready (`#ifdef __ANDROID__` gates in `port/creds.c`,
`port/resources.c`, `platform/netplay/discord_rpc.c`, `args.c`). Root
`CMakeLists.txt` has an `if(ANDROID)` branch that skips `libcdio` / `ffmpeg`
/ `tf-psa-crypto` (no ISO parsing, no in-game music, no checksum mode on
Android v1). The Gradle project here invokes the root CMakeLists via NDK
externalNativeBuild.

What still needs to happen end-to-end (NOT done in this commit):

1. **Cross-compile third_party deps for `arm64-v8a`** — see *Build deps* below.
2. **Copy SDL3 sources into this project** — `org.libsdl.app.SDLActivity`
   has to be on the Java classpath. Fetch from `libsdl-org/SDL` `release-3.x`
   branch, `android-project/app/src/main/java/org/libsdl/app/*.java`.
3. **Get a Gradle wrapper** — run `gradle wrapper` once in this directory
   (needs a host-installed Gradle 8.5+) to materialize `gradlew` + the
   wrapper jar.
4. **App icon resources** — drop a `mipmap/ic_launcher.png` into
   `app/src/main/res/mipmap-*/`.
5. **Build + adb install**:
   ```
   cd android-project
   ./gradlew assembleRelease
   adb install app/build/outputs/apk/release/app-release.apk
   ```

## Build deps for Android

NDK 26.1.10909125 is required (already installed via scoop android-clt).
Toolchain file:
`%ANDROID_HOME%/ndk/26.1.10909125/build/cmake/android.toolchain.cmake`

Each dependency under `../third_party/` ships a CMake project. To cross-
compile for arm64-v8a, invoke each with:

```
cmake -S <dep_src> -B <dep_src>/build-android \
  -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-29 \
  -DCMAKE_BUILD_TYPE=Release
cmake --build <dep_src>/build-android
```

Deps needed for v1:
- `sdl3` (build as shared lib → `libSDL3.so`; also extract its
  `android-project/app/src/main/java/org/libsdl/app/` Java sources)
- `SDL_net` (static `.a` ok)
- `GekkoNet` (static `.a` ok)
- `minizip-ng` (static `.a` ok)

Skipped on Android v1:
- `libcdio` (ISO 9660 parsing — Android takes pre-extracted AFS via SAF)
- `ffmpeg` (ADX audio — game is silent on Android v1)
- `tf-psa-crypto` (only used in `CHECKSUM` mode, disabled here)

## ROM provisioning (SAF flow)

On first launch the app checks for `<filesDir>/resources/SF33RD.AFS`. If
missing, `ThreeSXActivity.onCreate` launches the system file picker
(`ACTION_OPEN_DOCUMENT`) so the user can pick the AFS file from their
device. Selected file is copied into internal storage, then the Activity
recreates and SDL boots normally.

v1 requires an **already-extracted AFS** (use the Windows / macOS / Linux
build to extract from an ISO first, then transfer to the phone). Adding
ISO 9660 parsing on Android = future work (would need a tiny pure-Java ISO
reader since libcdio doesn't cross-compile cleanly).
