# 3SX

Native port of *Street Fighter III: 3rd Strike* with cross-platform rollback netplay. Single source tree builds for **Windows**, **macOS**, **Linux**, **Android** (phones + tablets + Android TV), and **PlayStation Vita**.

## Features

- Native 60 fps rendering on every target (no emulation).
- GekkoNet rollback netcode with prediction window + frame-skip backoff.
- Matchmaking server with regions, custom rooms, chat, and ELO ranking.
- LAN-direct fast-path for same-NAT peers (server intersects advertised LAN IPs on `/24` prefix).
- TCP fast-disconnect (~100 ms instead of gekko's UDP timeout) on peer crash.
- Hold-last-frame on any sim stall — no black flicker during rollback storms or peer AFS bursts.
- Chunked AFS sync I/O on slow storage (Vita flash) with TCP keep-alive pump.
- Per-platform credential storage: Windows DPAPI, Android sandboxed JSON, Vita plaintext token.
- Cross-platform Discord Rich Presence (desktop only).

## Legal

3SX is a **port of a decompilation** — engine code only. It does not contain, distribute, or recreate any Capcom-owned assets, audio, fonts, or graphics. To run it you need your own legally obtained copy of *Street Fighter III: 3rd Strike* (the PS2 disc, the Capcom Fighting Anniversary Collection disc, or your own backup of either) to extract `SF33RD.AFS` from.

**Do not distribute compiled builds that bundle `SF33RD.AFS`.** Pre-built artifacts produced by CI ship engine code only. If you build a personal copy with the AFS embedded inside the APK / VPK for convenience, keep it on your own devices.

## How to play

3SX needs your own dump of *Street Fighter III: 3rd Strike*. The repo ships engine code only — no assets.

### Windows

1. Download `3SX-x.y.z.zip` from the [Releases](https://github.com/SalieriMZ/3sx-online/releases) page.
2. Extract anywhere, double-click `3SX.exe`. The launcher walks you through ISO → AFS conversion on first run.
3. Click *Launch Game*. Auto-updates pick up new stable releases.

### Android

1. Download `3sx.apk` from the [Releases](https://github.com/SalieriMZ/3sx-online/releases) page.
2. Sideload via `adb install 3sx.apk` (or any file manager — enable *Install unknown apps*).
3. On first run the app prompts for `SF33RD.AFS` via the Android Storage Access Framework — point it at the file you produced from your PS2 disc. The shell copies it into the app's private data dir.

### PlayStation Vita

Pre-reqs: an exploited Vita (HENkaku / h-encore² / Adrenaline / etc.), VitaShell installed, ~600 MB free on `ux0:`. The OFW does not install homebrew VPKs.

1. Download `3sx.vpk` from the Releases page.
2. Copy it to `ux0:VPK/3sx.vpk` (USB / FTP / wireless via VitaShell — any transport).
3. Open VitaShell, navigate to `ux0:VPK/3sx.vpk`, press `X`, confirm the install.
4. **Stage `SF33RD.AFS` manually** (Vita has no SAF / native file picker). From your own ROM dump, drop the file at:

   ```
   ux0:data/3sx/resources/SF33RD.AFS
   ```

   The path is checked at boot — if it's missing the game prints a "Place SF33RD.AFS here" message and exits. The file is ~600 MB; copy via USB or FTP, not over a slow wireless link.
5. Launch from the LiveArea bubble. First launch opens the in-game region picker + on-screen-keyboard login.
   - **Region picker**: D-pad up/down to highlight, any face button to confirm.
   - **Login / register**: the Sce IME dialog opens for username + password. Triangle or Start cancels.
   - Pick *Create new account* the first time — Login fails if the account doesn't exist yet.

The Vita-specific token + region get saved to `ux0:data/CrowdedStreet/3SX/`; you only see the setup flow on first launch (or after deleting the token).

**Tip for personal builds:** If you'd rather bundle the AFS *inside* the VPK so re-installs don't lose the staged file, drop `SF33RD.AFS` into `vita/resources/` of the source tree before running `bash build.sh vita`. The build picks it up at `app0:/resources/SF33RD.AFS` and `Resources_GetAFSPath()` reads from there. **Do not share the resulting VPK** — it contains Capcom IP.

## Building

3SX uses one CMake project across all targets. Platform selection is automatic via the toolchain.

### Prerequisites (all targets)

- CMake ≥ 3.20
- Clang or GCC
- Python 3 (for dependency build scripts)

Run the dependency bootstrap once per target:

```sh
bash build-deps.sh           # desktop (Windows MSYS2 / macOS / Linux)
bash build-deps-android.sh   # Android NDK
bash build-deps-vita.sh      # vitasdk
bash build-ffmpeg-vita.sh    # custom Vita ffmpeg with adpcm_adx
```

These scripts cross-compile GekkoNet, SDL3, SDL_net, minizip-ng, ffmpeg, and (desktop only) libcdio + tf-psa-crypto into `third_party/<dep>/build*/`.

### Windows / macOS / Linux

```sh
CC=clang CXX=clang++ cmake -B build -DCMAKE_BUILD_TYPE=Release -G Ninja
cmake --build build --target 3sx
```

The binary lands at `build/3sx.exe` (Windows) or `build/3sx`. Drop the contents of `build/application/bin/` and your `SF33RD.AFS` (under `resources/`) into the same directory to run it standalone.

### Android (phones, tablets, TV)

Requires Android NDK 26.1.10909125 and JDK 17+. Set `ANDROID_HOME` to your Android SDK install. Multi-ABI (arm64-v8a + armeabi-v7a) is built automatically — covers modern phones and 32-bit Xiaomi TV.

```sh
cd android-project
./gradlew assembleDebug                       # signed with debug keystore, side-loadable
# or
./gradlew assembleRelease                     # unsigned; sign with apksigner if shipping
```

APK lands at `android-project/app/build/outputs/apk/<flavor>/`.

To **bundle your `SF33RD.AFS` inside the APK** so end-users don't have to stage it via SAF:

1. Copy `SF33RD.AFS` into `android-project/app/src/main/assets/`.
2. Rebuild. The Java shell already detects an assets-bundled AFS and copies it into private storage on first launch instead of prompting via SAF.

### PlayStation Vita

Requires Docker (uses the `vitasdk/vitasdk` image to avoid host glibc issues).

```sh
docker run --rm -v "$(pwd):/workspace" -w /workspace vitasdk/vitasdk:latest \
    sh -c "cmake -B build-vita -DCMAKE_TOOLCHAIN_FILE=\$VITASDK/share/vita.toolchain.cmake -DCMAKE_BUILD_TYPE=Release && cmake --build build-vita -j"
```

VPK lands at `build-vita/3sx.vpk` (~500 MiB — bundled AFS is what dominates size).

To **bundle `SF33RD.AFS` inside the VPK** (so the user doesn't have to manually drop the file at `ux0:data/3sx/resources/`):

1. Copy `SF33RD.AFS` into `vita/resources/` *before* the cmake configure step.
2. The Vita CMake branch already scoops anything under that directory into the VPK at `app0:/resources/`, and `Resources_GetAFSPath()` reads from there first.

If you'd rather ship a slim VPK and let users stage the file themselves, leave `vita/resources/` empty — the build still succeeds and the game shows a "place SF33RD.AFS at ux0:data/3sx/resources/" prompt at startup.

## Repo layout

```
src/                      C source tree (shared across all platforms)
  main.c                  Entry point + frame loop driver
  args.c, args.h          CLI + persisted netplay prefs
  platform/
    app/sdl/              SDL3 app driver (window, input, frame pacing)
    app/vita/             Vita-only login + region picker UI
    input/sdl/            SDL gamepad → SF3 io conversion
    netplay/              GekkoNet adapter + fistbump TCP protocol
    video/sdl_gpu/        Desktop renderer (SDL_GPU)
    video/opengl/         OpenGL renderer (Vita via vitaGL, fallback PC)
  port/
    io/afs.c              AFS file reader with chunked async I/O
    io/local_ip.c         Platform-specific LAN IP probe
    creds.c               DPAPI / sandboxed-JSON / plaintext token
    resources.c           SF33RD.AFS path resolver + ISO → AFS converter
  imgui/                  PC ImGui overlays (login, settings, chat)
  sf33rd/                 Decompiled SF3 game code (mostly untouched)

android-project/          Gradle + Java shell + Android-specific resources
vita/                     Vita VPK packaging (icon, livearea, sce_sys)
third_party/              SDL3, GekkoNet, ffmpeg, minizip-ng, libcdio, vitaGL...

build-deps.sh             Desktop dep cross-compile
build-deps-android.sh     Android dep cross-compile (calls NDK)
build-deps-vita.sh        Vita dep cross-compile (calls vitasdk via Docker)
build-ffmpeg-vita.sh      Custom Vita ffmpeg with adpcm_adx decoder
```

## Online play

Online matchmaking is handled by **fistbump**, an asyncio Python 3 server with SQLite persistence and ELO ranking. The reference implementation lives in the [fistbump-server](https://github.com/SalieriMZ/fistbump-server) repo — bring up your own instance and clients connect via the regions list (see below).

### Configure regions

Drop a `regions.txt` either next to the binary (`<install>/resources/regions.txt`) or in your prefpath (`<prefpath>/regions.txt` — overrides the bundled one). Format:

```
# code|label|host|port
us-east-1|US East|fistbump.example.com|19000
sa-east-1|South America|fistbump-sa.example.com|19000
local|Local|127.0.0.1|9000
```

See [`regions.example.txt`](regions.example.txt) for the full annotated template.

Pick your region in-game (PC: ImGui overlay, Vita: native picker, Android: settings panel). Login or register from the same screen — the server issues a refresh token after the first successful auth so subsequent launches log in automatically.

## Acknowledgments

- [GekkoNet](https://github.com/HeatXD/GekkoNet) — P2P rollback netcode
- [SDL3](https://github.com/libsdl-org/SDL) — windowing, input, audio, GPU
- [SDL_net](https://github.com/libsdl-org/SDL_net) — TCP + UDP socket layer
- [FFmpeg](https://ffmpeg.org) — ADX music playback
- [libcdio / libiso9660](https://github.com/libcdio/libcdio) — ISO parsing
- [vitaGL](https://github.com/Rinnegatamante/vitaGL) — OpenGL wrapper on Vita's libgxm
- [minizip-ng](https://github.com/zlib-ng/minizip-ng), [zlib](https://zlib.net) — compression
- [TF-PSA-Crypto](https://github.com/Mbed-TLS/TF-PSA-Crypto) — SHA256 checksums
- [argparse](https://github.com/cofyc/argparse), [stb](https://github.com/nothings/stb)
- [Dear ImGui](https://github.com/ocornut/imgui) — desktop in-game overlays

## Community

Join the *Crowded Street* Discord to report bugs, propose features, or find an opponent.

[![Join the Discord](https://dcbadge.limes.pink/api/server/https://discord.gg/wqs6BqYr8C)](https://discord.gg/wqs6BqYr8C)
