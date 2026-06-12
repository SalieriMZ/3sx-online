# 1.7.28 design — post-match flow, ranked integrity, multi-fight rooms

Status: in progress. Tracks the implementation of roadmap items 1–5 + multi-fight
rooms (overlay-first). Single design doc because three of the items are the same
code path: the REMATCH result bug, the EXIT routing, and multi-fight rooms all
live in what happens *after* an online match ends.

## Scope

| # | Item | Size |
|---|---|---|
| A1 | Upstream port [#255](https://github.com/crowded-street/3sx/pull/255) — BGM volume during pause | trivial |
| A2 | Upstream port [#254](https://github.com/crowded-street/3sx/pull/254) — hold-to-pause in versus | small |
| B | REMATCH result reporting fix (ranked integrity) | 1 line + tests |
| C | Post-match routing: room → Network menu (connection kept), queue → mode select | core |
| D | Platform + region telemetry → leaderboard columns | client+server |
| E | Multi-fight rooms v1: cumulative scoreboard, overlay UI | client+server |

Spectator mode (roadmap #5) is intentionally out — it builds on E and lands next.

## B — REMATCH result bug (root cause, verified)

`fistbump.c` guards result reporting with a `result_sent` flag so a finished
match reports exactly once (`Fistbump_SendResult`, guard at the top). The flag
is reset in exactly two places:

- `Fistbump_HandleMATCH` (queue path — `MATCH` always precedes `START`)
- the local `CANCEL` path

Room rematches skip `MATCH` entirely: the server's `_pair_room()` dispatches
`START` directly to both slotted players. So after the first room match sets
`result_sent = true`, **every subsequent match in the same room silently never
sends RESULT** — the server's PendingMatch never finalizes, ELO/W-L don't move,
and the janitor eventually reaps the match as abandoned.

**Fix:** reset `result_sent = false` in `Fistbump_HandleSTART`. START is the
single point every match path (queue, room, rematch) funnels through, so the
invariant becomes "a new match id always re-arms result reporting". The resets
in HandleMATCH/CANCEL stay (harmless, defensive).

## C — Post-match routing

### Current behaviour

`NETPLAY_SESSION_EXITING` (netplay.c) tears down gekko/sockets, calls
`Netplay_CancelMatchmaking()` — which already distinguishes in-room
(`Fistbump_EndMatch`: keeps TCP + room + profile) from queue
(`Fistbump_Reset`: full teardown) — then `Soft_Reset_Sub()` and drops to
`NETPLAY_SESSION_IDLE`. Nothing routes the menu task anywhere: the player
drifts through title/intermediate screens, and the preserved room connection
is invisible until they manually re-enter Network.

### Target behaviour

At the end of the EXITING block, route explicitly based on what survived:

- **In room** (`Fistbump_GetRoom() != NULL`): jump the menu task straight back
  into the **Network menu** routine. TCP session, login, and room membership
  are already preserved by `Fistbump_EndMatch`; the netplay overlay panel shows
  the room (now with scoreboard, see E). The player can re-slot / start the
  next fight without reconnecting.
- **Not in room** (queue match or full disconnect): jump straight to **mode
  select** — no title screen, no intermediate fades.

### Mechanism

`menu.c` routes the menu task through `After_Title`'s jump table on
`r_no[1]`: index 1 = `Mode_Select`, index 6 = `Netplay_Menu` (NETPLAY_ENABLED
builds). `Back_to_Mode_Select(task_ptr)` (menu.c) already performs a clean
landing on mode select (G_No/E_No setup + `Menu_Init` + BGM request). Plan:

1. Add two small entry points to menu.c (exported via menu.h):
   - `Menu_NetplayReturnToModeSelect()` — wraps the `Back_to_Mode_Select`
     G_No/E_No/Menu_Init sequence so the netplay layer can invoke it without
     owning a task pointer.
   - `Menu_NetplayReturnToNetwork()` — same landing but sets `r_no[1] = 6`
     (Netplay_Menu) so the first menu frame enters the Network screen.
     `Netplay_Menu` re-runs `Netplay_BeginMatchmaking()` on entry, which is a
     no-op when fistbump is already LOGGED_IN/in-room.
2. Call the appropriate one at the end of the `NETPLAY_SESSION_EXITING` block,
   after `Soft_Reset_Sub()` and the state cleanup, decided by
   `Fistbump_GetRoom() != NULL`.
3. The `Mode_Type = MODE_ARCADE` reset stays — `Netplay_Menu` sets
   `MODE_NETWORK` again on entry when needed.

Edge cases:
- Peer crash / TCP CANCEL mid-match while in room: server keeps the room
  (member drop is a separate LEAVE); if our TCP died, `Fistbump_GetRoom()`
  returns NULL after reset → lands on mode select. Correct.
- Decline / queue timeout paths don't touch EXITING and are unaffected.

## D — Platform + region telemetry

Today the protocol never transmits the platform; the leaderboard can't show
where anyone plays. Changes:

- **Client** (`fistbump.c`): append `BUILD_PLATFORM_STR` (already defined in
  `args.c` — move the define to a shared header) to `HELLO` / `LOGIN` /
  `REGISTER` / `REFRESH` as a trailing field. Old servers ignore extra fields.
- **Server** (`server.py`):
  - parse the optional platform field → `ClientSession.platform`
  - new CLI arg `--region-code` (default `unknown`) → stamped on matches
  - matches table migration: `ALTER TABLE matches ADD COLUMN platform_a/b TEXT`
    + `region TEXT` (idempotent `try/except` migration on startup)
  - edge → leader `/api/internal/result` carries `region` + platforms
  - leaderboard HTML: per-player aggregated `platforms` (distinct, e.g.
    `PC·Android`) + `regions` columns from their recent matches
- **Compat:** trailing-field protocol addition — 1.7.27 clients keep working
  (server treats missing field as `unknown`). No version gate bump required;
  ALLOWED_VERSIONS already includes 1.7.28 candidates via the launcher's
  on-disk version.

## E — Multi-fight rooms v1 (overlay UI)

Server-side rooms already persist across matches (members, slots, settings
incl. `best_of`, host-driven `START`). What v1 adds:

- **Server:** per-member cumulative win counter on the Room object, updated at
  result finalize time (`_after_match_finalize` knows the room via
  `current_match_id`). Reset when the room empties (rooms are already reaped
  when idle). Broadcast in the existing single-line `ROOM STATE` snapshot as
  `name:wins` pairs — additive field, old clients ignore it.
- **Client overlay** (`netplay_panel.c`):
  - scoreboard section: member list with cumulative wins, current slots
    highlighted
  - after C lands players back in the room post-match, the host's existing
    *Start Match* button is the "next fight" action — no new protocol needed
  - host can re-slot any two members between fights (already supported
    server-side via slot take/leave)
- **Out of scope for v1:** winner-stays auto-rotation, per-room best-of-N
  *series* tracking (the scoreboard is cumulative wins), spectator slots.

## Order of work

1. A1 + A2 upstream ports (independent, unblock early testing)
2. B rematch fix (one line; testable as soon as C lets us rematch quickly)
3. C post-match routing (unblocks E testing loop)
4. D telemetry (client protocol + server)
5. E room scoreboard (server first, then overlay)
6. Lockstep release: client 1.7.28 + server deploy (leader + sa-east-1 edge),
   `ALLOWED_VERSIONS += 1.7.28`, GitHub release with both flavours.

## Test plan

- B+C+E together: two local clients + local server, create room, play 3
  matches in a row; assert server `matches` rows = 3 finalized, scoreboard
  increments, EXIT lands in room view each time, leaving room + EXIT lands on
  mode select.
- D: login from PC + Android builds, check leaderboard HTML shows platforms;
  result via sa-east-1 edge shows region.
- A2: versus pause requires ~1 s hold; netplay match still cannot pause.
- Regression: queue casual match end-to-end on public server.
