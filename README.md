# 3SX Online

[![us-east-1](https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/SalieriMZ/3sx-online/status/status/us-east-1.json)](https://github.com/SalieriMZ/3sx-online/actions/workflows/server_status.yml)
[![sa-east-1](https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/SalieriMZ/3sx-online/status/status/sa-east-1.json)](https://github.com/SalieriMZ/3sx-online/actions/workflows/server_status.yml)
[![fistbump](https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/SalieriMZ/3sx-online/status/status/all.json)](https://github.com/SalieriMZ/3sx-online/actions/workflows/server_status.yml)
[![Discord](https://img.shields.io/badge/Discord-3SX%20Online-5865F2?logo=discord&logoColor=white)](https://discord.gg/aume4RqnnP)

A **community-built online layer** for *Street Fighter III: 3rd Strike* — rollback netcode + cross-region matchmaking + custom rooms + ELO, on top of the [crowded-street/3sx](https://github.com/crowded-street/3sx) native decompilation.

Builds from a single source tree for **Windows**, **macOS**, **Linux**, **Android** (phones + tablets + Android TV), and **PlayStation Vita**. PC ↔ Vita ↔ Android cross-play confirmed working on real hardware.

> **Independent community project.** We are not affiliated with [crowded-street](https://github.com/crowded-street/3sx) (the upstream decompilation) or with Capcom. Upstream focuses on the offline single-player experience; this fork is where the online + multi-platform work lives. Upstream improvements are ported here manually, so they can take a while to land in this fork. We ship engine code only — Capcom-owned assets remain Capcom's, you bring your own dump.

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

- **Discord** — the **3SX Online** server is where matchmaking happens, and where playtesting, bug reports, and announcements live: [discord.gg/aume4RqnnP](https://discord.gg/aume4RqnnP)
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

## Project status — read before playing

This is an early public release, play-tested by a small community. The core loop — log in, find a match, play with rollback — is solid across PC, Android, and Vita, but **some flows are still rough and some features are unfinished**. Things you may run into:

- **No in-room rematch yet** — after an online match ends you return to the menu and re-queue (or restart from the room lobby). A proper rematch flow is on the roadmap.
- **REMATCH on the same connection has bugs** — re-fighting the same opponent without tearing the session down can desync result reporting, which means **some ranked results don't count the way they should**. Until that's fixed, take ranked standings with a grain of salt.
- **Stat tracker / ELO / ranks are early** — the [community leaderboard](https://sf3-leaderboard.chambeadores.cl/) is live so you can see who's playing, but the rating system is young: expect inconsistencies, and possibly a reset before rankings become meaningful.
- **Queue decline / timeout UX is incomplete** — declining a match or letting the accept dialog time out can leave the UI in an odd state; backing out to the Network menu recovers it.
- **Custom rooms are an MVP** — slots, settings, and chat work, but multi-fight lobbies are not finished.
- **Android TV (32-bit `armeabi-v7a`)** installs have been flaky and are untested on the current tree.
- Occasional character-select slowdown on Vita with slow storage (largely fixed, not 100%).

If you hit something not listed here, report it on [Discord](https://discord.gg/aume4RqnnP) or [GitHub Issues](https://github.com/SalieriMZ/3sx-online/issues) — include the output of `3sx --version` and your `netplay.log` if you can.

## Roadmap

Rough priority order — no dates promised:

1. **Post-match flow fixes** — fix the result-reporting bugs when rematching on the same connection (so ranked counts reliably), make *EXIT* after a match drop you straight back to the main screen, and add a proper in-room rematch.
2. **Multi-fight custom rooms** — SF6-style lobbies: up to 8 members, host picks the next two fighters, best-of-N, cumulative score.
3. **Direct versus by IP** — play a friend with no matchmaking server at all. The netcode already supports it (`--p2p-local-player` / `--p2p-remote-ip`); it needs an in-game UI.
4. **Leaderboard & stat tracker** — show which **regions** and **platforms** (PC / Android / Vita) each player fights on. Needs some client-side telemetry additions, so it lands together with a client update.
5. **Replays + spectator mode** — record/watch matches; live spectating from a room.
6. **PlayStation Vita release builds** — Vita is fully playable from source today; pre-built VPKs return in a follow-up release.
7. **Android quality-of-life** — touch controls, lifecycle pause/resume polish, in-app updates.
8. **In-game UI consolidation** — move flows into native game menus so we depend less on the ImGui overlay and remove the redundancy between the two.
9. **If the project gains traction** — validated character/match stats, hardened netplay protocol, and anti-cheat measures so ranked stays trustworthy.

Upstream [crowded-street/3sx](https://github.com/crowded-street/3sx) keeps improving the offline game; we port those changes manually, so they may take longer to arrive in this fork.

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

| Platform | User override (highest priority) | OS-managed prefs dir | Bundled default |
|---|---|---|---|
| Windows | `%APPDATA%\CrowdedStreet\3SX\regions.txt` | (same) | `<install>\regions.txt` then `<install>\resources\regions.txt` |
| macOS | `~/Library/Application Support/CrowdedStreet/3SX/regions.txt` | (same) | `<install>/regions.txt` then `<install>/resources/regions.txt` |
| Linux | `~/.local/share/CrowdedStreet/3SX/regions.txt` | (same) | `<install>/regions.txt` then `<install>/resources/regions.txt` |
| Android | `/data/data/cl.chambeadores.threesx/files/regions.txt` | (same — copied from `assets/regions.txt` on first launch) | APK `assets/regions.txt` |
| Vita | `ux0:data/CrowdedStreet/3SX/regions.txt` | (same) | `app0:/resources/regions.txt` |

A starter template is at [`regions.example.txt`](regions.example.txt), or run `bash setup-regions.sh` for an interactive prompt that asks for your server IPs and writes the file for you. Either ship your own list with the build (per the build steps below) or drop one alongside the installed binary.

You can confirm which path the game actually loaded by running `3sx --version` and then checking `netplay.log` in the prefs dir — a line like `[REGION] loaded 2 region(s) from basepath=...` shows the resolution path.

### Deploying with your own infrastructure

The reference matchmaking server lives at [`SalieriMZ/fistbump-server`](https://github.com/SalieriMZ/fistbump-server). If you're running your own instance and packaging a build for your community, the knobs you'll want to know about are:

| What | Where | Notes |
|---|---|---|
| Matchmaking host + port | `regions.txt` (`code\|label\|host\|port` lines) | Up to 16 entries. The game pings each at startup and the in-game picker auto-selects the lowest-latency one. |
| Discord Rich Presence app | `-DDISCORD_APP_ID=<id>` at cmake configure time | Register an app at <https://discord.com/developers/applications>. Omit the flag for an RPC-free build (presence is a no-op stub). |
| Launcher update URL | `FISTBUMP_UPDATE_BASE_URL` env var when launching the launcher | Defaults to `http://<first-region-host>:20000`. Point at your stats/HTTP endpoint to enable auto-updates. Failures are silent — the launcher always lets the user click *Play*. |
| Launcher region list (override) | `FISTBUMP_REGIONS="code\|label\|host\|port;..."` env var | Overrides the launcher's default region dropdown without touching `regions.txt` on the game side. |
| Build version stamp | `-DBUILD_VERSION=1.7.27` at cmake configure | Printed by `3sx --version`. Auto-detected from `CMakeLists.txt` if omitted. |
| Build git SHA stamp | `-DBUILD_GIT_SHA=abc1234` at cmake configure | Auto-detected via `git rev-parse --short HEAD` if you build from a checkout. |
| Android package name | `applicationId` in `android-project/app/build.gradle` | Defaults to `cl.chambeadores.threesx` (the reference community build). Change it before publishing your own APK so installs don't collide — and keep the `PKG` variable in `android-project/install-and-run.bat` in sync. |
| Server version gate | `ALLOWED_VERSIONS` in `fistbump-server/server.py` | The server rejects clients whose version string isn't whitelisted. If you ship your own client builds, add each new version **before** rolling it out, or players get a login error. |
| Status badges | `.github/workflows/server_status.yml` | Probes the hosts listed at the top of the workflow — change them to your own. Results are committed as shields.io JSON to an auto-generated `status` branch; never edit that branch by hand, the workflow overwrites it. |

For a complete walk-through (clone → regions.txt → packaged zip), see `dist.sh` — invoking `bash dist.sh pc 1.7.27` with `REGIONS_FILE=path/to/regions.txt` and `INCLUDE_LAUNCHER=path/to/3sx_launcher_online.exe` produces a ready-to-share zip under `dist/`. The DLL closure is resolved with `objdump`, so the zip ships only what the binary actually links — no stray `/mingw64/bin` codecs.

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

To verify what you built, run:

```bash
build/application/bin/3sx.exe --version
```

This prints version + git SHA + platform + build date + Discord App ID + whether netplay is enabled. Useful when triaging "which build is the user actually running" reports.

To package a ready-to-share zip with the DLL closure pre-pruned, run:

```bash
DISCORD_APP_ID=<id> REGIONS_FILE=path/to/regions.txt bash dist.sh pc 1.7.27
```

Output is at `dist/3sx-1.7.27-pc.zip`.

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

#### Android caveats

- **ABI**: the build script targets `arm64-v8a` only. Override with `ANDROID_ABI=x86_64` or `armeabi-v7a` env vars when invoking `bash build.sh android`, but third-party deps need to be re-cross-compiled for that ABI first (run `ABI=x86_64 bash build-deps-android.sh`).
- **API level**: targets API 29 (Android 10). Override with `ANDROID_API=` if you need an older floor — deps will need to be re-built.
- **Input**: there are no on-screen touch controls. A Bluetooth gamepad (or any HID gamepad over USB-C OTG) is required to play.
- **Audio**: ADX music depends on the cross-compiled FFmpeg libs being present in `jniLibs/<abi>/`. `build.sh android` copies them automatically; if you're running `gradlew assembleDebug` directly you need to stage them yourself.
- **Updates**: the Android build has no launcher / in-app update flow. New versions ship as new APKs.

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
dist.sh                   Packager: `bash dist.sh <pc|android|vita> [version]`
                          produces dist/3sx-<version>-<target>.zip with DLL
                          closure resolved + optional regions.txt + launcher.
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

**AGPL-3.0**, inherited from the upstream [crowded-street/3sx](https://github.com/crowded-street/3sx) decompilation. See [`LICENSE`](LICENSE). In short: you can use, modify, and redistribute freely, but derivative works — including hosted/networked forks — must publish their source under the same license.
