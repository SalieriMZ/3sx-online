# 3SX Online

A **community-built online layer** for *Street Fighter III: 3rd Strike* — rollback netcode + cross-region matchmaking + custom rooms + ELO, on top of the [crowded-street/3sx](https://github.com/crowded-street/3sx) native decompilation.

Builds from a single source tree for **Windows**, **macOS**, **Linux**, **Android** (phones + tablets + Android TV), and **PlayStation Vita**. PC ↔ Vita ↔ Android cross-play confirmed working on real hardware.

> Upstream `crowded-street/3sx` focuses on the offline single-player experience. This fork is where the online + multi-platform work lives. We ship engine code only — Capcom-owned assets remain Capcom's, you bring your own dump.

---

## Why this exists

3rd Strike is 26 years old and still has the most expressive defense in the genre. The official options to play it online are limited, region-locked, and dependent on Capcom's continued maintenance. This project:

- Brings **rollback netcode** (GekkoNet) to native 3rd Strike.
- Pairs you with opponents across the world via a self-hostable matchmaking server.
- Runs natively on hardware that the official builds don't target — Android phones, Android TV, and PlayStation Vita.
- Treats Vita ↔ PC and Android ↔ PC matches as first-class — same protocol, same regions, same ELO pool.
- Is **fully self-hostable**: bring up your own matchmaking server and your community owns the infrastructure.

---

## Community

- **Discord** — the *Crowded Street* server is where playtesting, bug reports, and feature design happen. [![Join the Discord](https://dcbadge.limes.pink/api/server/https://discord.gg/wqs6BqYr8C)](https://discord.gg/wqs6BqYr8C)
- **Issues** — file bugs / feature requests at [GitHub Issues](https://github.com/SalieriMZ/3sx-online/issues).
- **Reference matchmaking server** — [`SalieriMZ/fistbump-server`](https://github.com/SalieriMZ/fistbump-server). Run your own or talk to us about joining a hosted instance.

---

## Online features at a glance

- GekkoNet rollback with prediction window + adaptive frame-skip on cross-region links.
- Region picker with live ping (TCP RTT to the matchmaking host).
- Login / register via in-game overlay (PC + Android use ImGui; Vita uses the native Sce IME dialog).
- Quick match (casual / ranked) + custom rooms with shareable codes + chat + slot reservations.
- LAN-direct fast-path: when both peers are behind the same NAT (e.g. same WiFi) the server detects the shared `/24` prefix and skips the relay.
- Relay fallback for CGNAT / symmetric-NAT carriers (default-on for Android, off elsewhere).
- TCP fast-disconnect: ~100 ms tear-down when a peer crashes (vs gekko's ~30 s UDP timeout).
- Hold-last-frame on any sim stall — no black flicker during rollback storms or peer AFS bursts.
- Chunked AFS sync I/O on slow storage with TCP keep-alive pump (Vita flash specifically).

---

## How to play

3SX needs your own dump of *Street Fighter III: 3rd Strike*. The repo ships engine code only — no assets.

### Windows

1. Download `3SX-x.y.z.zip` from the [Releases](https://github.com/SalieriMZ/3sx-online/releases) page.
2. Extract anywhere, double-click `3SX.exe`. The launcher walks you through ISO → AFS conversion on first run.
3. Drop a `regions.txt` next to `3sx.exe` so the region picker has somewhere to point — see [Configuring regions](#configuring-regions) below.
4. Click *Launch Game*. The launcher auto-updates from the channel you pick (stable/beta).

### Android

1. Download `3sx.apk` from the [Releases](https://github.com/SalieriMZ/3sx-online/releases) page.
2. Sideload via `adb install 3sx.apk` (or any file manager — enable *Install unknown apps*).
3. On first run the app prompts for `SF33RD.AFS` via the Android Storage Access Framework — point it at the file you produced from your PS2 disc. The shell copies it into the app's private data dir.
4. Network panel reads `regions.txt` from the app's private `files/` dir. The repo's example file is bundled at first install; replace it through any file manager or rebuild with your own.

### PlayStation Vita

Pre-reqs: an exploited Vita (HENkaku / h-encore² / Adrenaline / etc.), VitaShell installed, ~600 MB free on `ux0:`. The OFW does **not** install homebrew VPKs.

1. Download `3sx.vpk` from the Releases page.
2. Copy it to `ux0:VPK/3sx.vpk` (USB / FTP / wireless via VitaShell — any transport).
3. Open VitaShell, navigate to `ux0:VPK/3sx.vpk`, press `X`, confirm the install.
4. Stage `SF33RD.AFS` at `ux0:data/3sx/resources/SF33RD.AFS` (Vita has no file picker; do this manually).
5. Drop a `regions.txt` at `ux0:data/3sx/resources/regions.txt` (or `ux0:data/CrowdedStreet/3SX/regions.txt` for user overrides).
6. Launch from the LiveArea bubble. First launch opens the in-game region picker + Sce IME login.
   - **Region picker**: D-pad up/down, any face button to confirm.
   - **Login / register**: the Sce IME dialog opens for username + password. Triangle or Start cancels.
   - Pick *Create new account* the first time — Login fails if the account doesn't exist yet.

### Configuring regions

Every client reads a `regions.txt` file at startup to populate the in-game region picker. Format is plain text, max 16 entries:

```
# code|label|host|port
us-east-1|US East|fistbump.example.com|19000
sa-east-1|South America|fistbump-sa.example.com|19000
local|Local|127.0.0.1|9000
```

The file is looked up in this order (first match wins):

| Platform | User override | Bundled default |
|---|---|---|
| Windows / macOS / Linux | `<install>/regions.txt` | `<install>/resources/regions.txt` |
| Android | `<app private files>/regions.txt` | bundled assets |
| Vita | `ux0:data/CrowdedStreet/3SX/regions.txt` | `app0:/resources/regions.txt` |

A starter template is at [`regions.example.txt`](regions.example.txt). Either ship your own list with the build (per the build steps below) or drop one alongside the installed binary.

---

## Building from source

The repo ships a single CMake project + a small orchestrator script. Pick one platform at a time.

### Prerequisites (all targets)

- **Git** with submodules enabled.
- **CMake ≥ 3.20**.
- **Python 3.10+** (used by `build-deps*.sh` for some packaging steps).
- **Ninja** (faster than make; required by some toolchains).

### Windows (MSYS2 MinGW64)

Tested on Windows 10 + 11. Visual Studio is *not* used.

1. Install MSYS2 from <https://www.msys2.org>.
2. Open the **MinGW64** shell (not the MSYS2 ucrt64 shell) and install the toolchain:

   ```bash
   pacman -S --needed \
       make \
       mingw-w64-x86_64-cmake \
       mingw-w64-x86_64-ninja \
       mingw-w64-x86_64-nasm \
       mingw-w64-x86_64-clang \
       mingw-w64-x86_64-zlib \
       mingw-w64-x86_64-headers-git \
       mingw-w64-x86_64-git
   ```

3. Clone with submodules:

   ```bash
   git clone --recurse-submodules https://github.com/SalieriMZ/3sx-online.git
   cd 3sx-online
   ```

4. Build dependencies (first time only, ~5 min):

   ```bash
   bash build-deps.sh
   ```

5. Build the game:

   ```bash
   bash build.sh pc
   ```

   Equivalent long form:

   ```bash
   CC=clang CXX=clang++ cmake -B build -DCMAKE_BUILD_TYPE=Release -G Ninja
   cmake --build build --target 3sx
   cmake --install build --prefix build/application
   ```

6. The runtime is at `build/application/bin/3sx.exe`. Drop `SF33RD.AFS` next to it in `resources/SF33RD.AFS` and double-click to run.

To embed your Discord application ID for Rich Presence, pass `-DDISCORD_APP_ID=<id>` to the first cmake invocation. Omit for an RPC-free build.

### Android

Tested with NDK r26b on Linux + macOS + Windows hosts. Use a separate clone — Android cross-compile writes to different `build*` dirs and gets confused by leftover MSYS state.

1. Install **JDK 17** (`temurin17-jdk` works) and **Android Studio** or just the [command-line tools](https://developer.android.com/studio#command-line-tools-only).
2. Use the SDK Manager to install **NDK 26.1.10909125** (the `r26b` release).
3. Set `ANDROID_NDK_HOME` to the NDK install (e.g. `~/Library/Android/sdk/ndk/26.1.10909125` on macOS).
4. Clone with submodules and bootstrap deps:

   ```bash
   git clone --recurse-submodules https://github.com/SalieriMZ/3sx-online.git
   cd 3sx-online
   bash build-deps-android.sh    # cross-compiles GekkoNet + SDL3 + ffmpeg etc.
   ```

5. Build the APK:

   ```bash
   bash build.sh android
   ```

   Or directly via Gradle:

   ```bash
   cd android-project
   ./gradlew assembleDebug
   ```

6. Output APK: `android-project/app/build/outputs/apk/debug/app-debug.apk`. Install with:

   ```bash
   adb install -r app-debug.apk
   ```

**To bundle `SF33RD.AFS` inside the APK** (so users don't need SAF on first run), drop it at `android-project/app/src/main/assets/SF33RD.AFS` *before* building. **Do not redistribute the resulting APK** — it contains Capcom IP.

### PlayStation Vita

Uses the vitasdk toolchain. The simplest path is the official Docker image, so contributors don't need a host install.

1. Install Docker Desktop (or the equivalent on Linux).
2. Clone with submodules:

   ```bash
   git clone --recurse-submodules https://github.com/SalieriMZ/3sx-online.git
   cd 3sx-online
   ```

3. Build via the orchestrator — pulls `vitasdk/vitasdk:latest`, runs the deps + ffmpeg + VPK packaging steps inside the container:

   ```bash
   bash build.sh vita
   ```

   The resulting VPK is at `build-vita/3sx.vpk` (~10 MB engine-only, ~600 MB with a bundled AFS).

   Equivalent long form, if you'd rather drive Docker yourself:

   ```bash
   docker run --rm -v "$(pwd):/workspace" -w /workspace vitasdk/vitasdk:latest sh -c "
     apk add --no-cache bash cmake ninja python3
     bash build-deps-vita.sh
     bash build-ffmpeg-vita.sh
     cmake -B build-vita \
       -DCMAKE_TOOLCHAIN_FILE=\$VITASDK/share/vita.toolchain.cmake \
       -DCMAKE_BUILD_TYPE=Release \
       -G Ninja
     cmake --build build-vita --target 3sx.vpk -j
   "
   ```

**To bundle `SF33RD.AFS` inside the VPK** (so users don't need to stage it under `ux0:` later), drop it at `vita/resources/SF33RD.AFS` *before* the cmake configure step. The build picks it up at `app0:/resources/SF33RD.AFS` automatically. **Do not redistribute the resulting VPK.**

### macOS / Linux

Vanilla clang + cmake + ninja toolchains. Run `bash build-deps.sh` then `bash build.sh pc`. Installed package goes to `build/application/`.

### Continuous integration

`.github/workflows/` builds every target on every push. The PSP and macOS builds run on push; Android, Vita, Windows, Linux, and PSP build matrices run on PR. Workflows fail upload if `SF33RD.AFS` ever ends up inside an artifact.

---

## Repo layout

```
src/
  main.c                  Entry point + frame loop driver.
  args.c, args.h          CLI + persisted netplay prefs.
  platform/
    app/sdl/              SDL3 app driver (window, input, frame pacing).
    app/vita/             Vita-only login + region picker UI.
    input/sdl/            SDL gamepad → SF3 io conversion.
    netplay/              GekkoNet adapter + fistbump TCP protocol client.
    video/sdl_gpu/        Desktop renderer (SDL_GPU).
    video/opengl/         OpenGL renderer (Vita via vitaGL, fallback PC).
  port/
    io/afs.c              AFS file reader with chunked async I/O.
    io/local_ip.c         Platform-specific LAN IP probe.
    creds.c               DPAPI / sandboxed-JSON / plaintext token storage.
    resources.c           SF33RD.AFS path resolver + ISO → AFS converter.
  imgui/                  PC ImGui overlays (login, settings, chat).
  sf33rd/                 Decompiled SF3 game code (do not edit).

android-project/          Gradle + Java shell + Android-specific resources.
vita/                     Vita VPK packaging (icon, livearea, sce_sys).
third_party/              SDL3, GekkoNet, ffmpeg, minizip-ng, libcdio, vitaGL.

build-deps.sh             Desktop dep cross-compile.
build-deps-android.sh     Android dep cross-compile (calls NDK).
build-deps-vita.sh        Vita dep cross-compile (calls vitasdk via Docker).
build-ffmpeg-vita.sh      Custom Vita ffmpeg with adpcm_adx decoder.
build.sh                  One-shot orchestrator: `bash build.sh <pc|android|vita>`.
regions.example.txt       Annotated template for the runtime region list.
```

---

## Contributing

See [`CONTRIBUTING.md`](CONTRIBUTING.md) for branching strategy, style notes, testing tips, and how to file a PR.

---

## Acknowledgements

- [crowded-street/3sx](https://github.com/crowded-street/3sx) — the upstream native decompilation that everything here builds on. Without their work this project does not exist.
- [GekkoNet](https://github.com/HeatXD/GekkoNet) — P2P rollback netcode.
- [SDL3](https://github.com/libsdl-org/SDL) — windowing, input, audio, GPU.
- [SDL_net](https://github.com/libsdl-org/SDL_net) — TCP + UDP socket layer.
- [vitaGL](https://github.com/Rinnegatamante/vitaGL) — OpenGL wrapper on Vita's libgxm.
- [FFmpeg](https://ffmpeg.org) — ADX music playback (custom build on Vita for the adpcm_adx decoder).
- [libcdio / libiso9660](https://github.com/libcdio/libcdio) — ISO parsing for the first-run AFS conversion flow.
- [minizip-ng](https://github.com/zlib-ng/minizip-ng), [zlib](https://zlib.net) — compression.
- [TF-PSA-Crypto](https://github.com/Mbed-TLS/TF-PSA-Crypto) — SHA256 checksums.
- [Dear ImGui](https://github.com/ocornut/imgui) — desktop in-game overlays.

## License

MIT. See [`LICENSE`](LICENSE).
