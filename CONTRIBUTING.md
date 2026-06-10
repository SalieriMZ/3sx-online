# Contributing to 3SX

3SX is a friendly fork of [crowded-street/3sx](https://github.com/crowded-street/3sx) that focuses on **cross-platform online play** (PC + Android + PlayStation Vita) on top of the upstream native port. Upstream concentrates on the offline experience; this fork ships matchmaking, rollback netcode, custom rooms, ELO, regions, and the other glue around them.

If you want to contribute, this file describes the workflow, the layout of the repos, and a few hard-won conventions that aren't obvious from reading the code.

## Repos

| Path | Purpose |
|---|---|
| [`SalieriMZ/3sx-online`](https://github.com/SalieriMZ/3sx-online) | The game client. Everything that runs on your PC / phone / Vita. |
| [`SalieriMZ/fistbump-server`](https://github.com/SalieriMZ/fistbump-server) | The fistbump matchmaking server + the desktop launcher. |

The client tracks upstream `crowded-street/3sx` as the `upstream` remote. Game-engine fixes flow upstream-first whenever it makes sense; netplay-specific work lives here.

## Branching

- `main` — what's currently shipping. Tagged `stable-X.Y.Z` after each successful promotion.
- `beta` — staging branch. Fixes land here first, get tested end-to-end across all three platforms, then promote to `main`.
- Feature branches → PR against `beta`.

Each release tag triggers the launcher's auto-update flow if you've also bumped:

1. `launcher/launcher.py` → `APP_VERSION`
2. `server.py` → `ALLOWED_VERSIONS` (add the new entry; don't remove the old ones).

These two have to move together with the client release — otherwise existing clients are kicked with `REJECT version mismatch`.

## Building locally

The repo ships `build.sh` as a one-liner orchestrator. From the client repo root:

```sh
bash build.sh pc        # MSYS2 MinGW64 on Windows; clang+ninja on Linux/macOS
bash build.sh android   # JDK 17 + Android NDK 26.1 (ANDROID_NDK_HOME)
bash build.sh vita      # uses Docker; no host toolchain required
```

Or follow the long form documented in [`README.md`](README.md). The CI workflows under `.github/workflows/` use the same commands, so anything that builds locally should turn green there.

If you change any of `build-deps*.sh`, the third_party cache key changes — first CI run on the PR will be slow.

## Hosting your own matchmaking server

A reference deployment is documented in the [`SalieriMZ/fistbump-server`](https://github.com/SalieriMZ/fistbump-server) README. Two ways:

1. Run `python server.py` locally on `localhost:9000` for development.
2. Use `deploy.sh` to ship the systemd unit to your own Linux box (set `REMOTE_HOST`, `SSH_KEY`, `PUBLIC_HOST` env vars).

Clients pick a matchmaking host through a `regions.txt` file (see `regions.example.txt`). There is **no hardcoded production server** in the client codebase — that's intentional. Forks can populate their own list.

## Style

- **C** — no formatter, broadly matches upstream `crowded-street/3sx`. 4-space indent, snake_case, opening brace on the same line.
- **Comments** — explain *why* something is non-obvious. Don't restate the code. A surprising workaround or invariant deserves a comment; a name change does not.
- **Commit messages** — lowercase subject, imperative mood, < 70 chars. Body wrap at 72. Don't add `Co-Authored-By` trailers when AI tools are involved — the diff stands on its own.

## Testing

`build.sh pc` is the smoke test. For online changes:

- Spin up a local fistbump (`python server.py --tcp-port 9000`), point the client at it via `FISTBUMP_HOST=127.0.0.1 FISTBUMP_PORT=9000`, and exercise REGISTER / LOGIN / QUEUE / MATCH.
- `test_client.py` in the server repo simulates two peers — useful for matchmaking changes that don't need the game running.
- `test_*.py` in the server repo cover chat/ELO/relay/decline E2E.

CI runs the full PC + macOS + Android + Vita matrix on every PR. Don't merge with red CI unless you know exactly why it's failing.

## Filing issues / PRs

- Bugs that affect online play: include the relevant `netplay.log` slice (it's at `<prefpath>/netplay.log` on every platform) and the **build sha** if you grabbed a pre-built artifact.
- New features: chat with us on Discord first if it's invasive — the netplay layer has more subtle invariants than the file count suggests.

## Legal / asset distribution

This repo ships engine code only. **Never commit `SF33RD.AFS` or any other Capcom-owned asset** to either the client or the server repo, and never publish a build artifact (APK, VPK, zip) that contains it. CI uploads from `.github/workflows/` only ever contain the engine binaries; if you build a personal copy with the AFS bundled (the `vita/resources/` or `android-project/app/src/main/assets/` shortcuts) keep it on your own devices.

If you're adding a release flow, lean toward smoke-checking the artifact for the AFS magic bytes (`AFS\0`) and failing the job if found — better to catch a leak in CI than in the wild.

## Acknowledgements

This work would not be possible without the upstream [crowded-street/3sx](https://github.com/crowded-street/3sx) decompilation project. Sega CD font, Capcom IP, etc. — everything Capcom-shaped remains theirs; this codebase ships none of it.

Thanks also to the GekkoNet, vitaGL, and SDL3 maintainers for tools that make this fork's three-platform online play feasible.
