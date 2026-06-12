# 3SX Android

Android-specific glue for 3SX Online. The C/C++ game lives under `../src/`
and is built by the root `CMakeLists.txt` via NDK `externalNativeBuild`; this
directory holds the Gradle project, the Java shell (`ThreeSXActivity` on top
of SDL's `SDLActivity`), and packaging helpers.

## Status

Fully playable: rendering, **ADX music + sound effects** (cross-compiled
FFmpeg), online matchmaking + rollback netplay, landscape lock, steady
60 fps. Tested on real arm64-v8a hardware (Android 10+). A Bluetooth or
USB-OTG **gamepad is required** — there are no on-screen touch controls yet.

Known gaps: touch controls, lifecycle pause/resume polish, Android TV
(32-bit `armeabi-v7a`) untested on the current tree, no in-app updates
(new versions ship as new APKs on the
[Releases page](https://github.com/SalieriMZ/3sx-online/releases)).

## Building

From the repo root:

```bash
bash build-deps-android.sh   # once: cross-compiles SDL3, SDL_net, GekkoNet, minizip-ng, FFmpeg for arm64-v8a
bash build.sh android        # cmake (NDK toolchain) → stages .so into jniLibs/ → gradlew assembleDebug
```

Requirements: JDK 17 + Android SDK with **NDK 26.1.10909125** (r26b); point
`ANDROID_NDK_HOME` at it. Output:
`app/build/outputs/apk/debug/app-debug.apk`.

- Override ABI / API floor with `ANDROID_ABI=` / `ANDROID_API=` env vars
  (third-party deps must be re-cross-compiled for the new ABI first).
- Bake Discord Rich Presence with `DISCORD_APP_ID=<id>`.
- `build.sh android` stages `lib3sx.so` + `libSDL3.so` + the FFmpeg `.so`
  files + `libc++_shared.so` into `app/src/main/jniLibs/<abi>/` before
  invoking Gradle. If you run `./gradlew assembleDebug` directly, you must
  stage those yourself or the APK ships without native libs.

## Getting SF33RD.AFS onto the device

The game expects `SF33RD.AFS` (extracted from **your own** PS2 disc —
Capcom IP, never included in released APKs) at
`<filesDir>/resources/SF33RD.AFS` inside the app sandbox. Two ways:

1. **`install-and-run.bat` (recommended)** — phone over USB with debugging
   enabled. Installs the APK, pushes the AFS into the sandbox via
   `adb` + `run-as`, stages shaders, and launches the game. Skips the
   ~600 MB push when the file is already on the device with the right size.
2. **Personal bundled build** — drop `SF33RD.AFS` at
   `app/src/main/assets/SF33RD.AFS` *before* building. On first launch the
   activity copies it from the APK into the sandbox. **Do not distribute
   the resulting APK** — it contains Capcom-owned data.

If the ROM is missing at launch, the app shows a toast and exits. An in-app
file picker (SAF) so users can stage the AFS without a PC is on the roadmap.

## regions.txt

The community APK bundles `assets/regions.txt` pointing at the public
matchmaking regions (the default/fork APK ships none). On first launch it's
copied to `<filesDir>/regions.txt`, where the native region picker reads it.
To point at your own servers, replace that file via `adb run-as` or rebuild
with your own `assets/regions.txt` — see the root README's
*Deploying with your own infrastructure* section.

## Useful commands

```bash
adb logcat -s SDL:* 3SX:* DEBUG:* AndroidRuntime:*        # live logs
adb shell "run-as cl.chambeadores.threesx ls -la files/resources"
adb shell "run-as cl.chambeadores.threesx cat files/regions.txt"
```
