# Rollback determinism contract

3SX online uses GekkoNet rollback netcode. Rollback only works if the
simulation is **deterministic** and the **entire mutable simulation state is
captured in the save-state**. When two peers feed the same inputs to the same
starting state, they must reach byte-identical states every frame. If any
mutable value the sim reads is left out of the save-state — or mutated outside
the stepped/rolled-back sim — the peers drift and **desync permanently**. Ping
is irrelevant to a correct deterministic core; a desync is a determinism bug,
never "a bad connection".

This document is the contract a contributor must respect when touching battle
code. It is the missing half of the netcode: the save-state is otherwise
well-built, so the danger is a *new* mutable global silently escaping it.

## The two save-state surfaces

State is captured in TWO places. **Both** must be checked before declaring a
value "saved":

1. **`GameState` / `GS_SAVE` / `GS_LOAD`** — `platform/netplay/game_state.{c,h}`.
   A flat struct mirroring ~566 game globals (`Round_num`, `C_No[]`, `plw[2]`,
   `vit[2]`, RNG indices, the `task[]` machine, etc.). `GameState_Save` copies
   each global → struct; `GameState_Load` copies back.

2. **`EffectState`** — inside `platform/netplay/netplay.c`. The effect/particle
   pool (`frw[EFFECT_MAX][448]`, `head_ix`/`tail_ix`/`exec_tm`/`frwque`/`frwctr`)
   lives outside `GameState` and is gathered/restored symmetrically in
   `gather_state` / `load_state`.

`State = { GameState gs; EffectState es; }` is what GekkoNet saves and rolls
back. A value is only safe if it is in `GameState` (surface 1) **or**
`EffectState` (surface 2). Grepping only `game_state.c` is the classic mistake
(the effect pool isn't there but IS saved).

## The cardinal rule

> **Every mutable global the simulation reads during a frame must be in the
> save-state, and must only be mutated from inside the stepped/rolled-back sim.**

Two ways to break it:

- **Unsaved mutable state.** A module-level `static`/global the sim reads but
  that isn't in either surface. On rollback the sim re-reads a value that was
  never rewound → divergence. (GGPO's docs warn specifically about "static
  automatic variables in C" for exactly this reason.)
- **Out-of-band mutation.** Mutating sim-visible state from OUTSIDE the
  `process_events → advance_game → step_game` path — e.g. on a wall-clock timer
  that ticks at a different rate on each peer. Even if the field is "saved", if
  something writes it between frames on real time, the two peers diverge.

### Known live instance: the AFS load-request queue (`q_ldreq`)

The current round-transition desync is exactly this class. The load-request
queue family in `sf33rd/Source/Game/io/gd3rd.c` —
`q_ldreq[16]`, `ldreq_break`, `plt_req[2]`, `ldreq_result[294]`,
`afs_io_in_progress`, `afs_handle` — is in **neither** save surface, **and** is
pumped out-of-band by the netplay AFS keepalive (`Check_LDREQ_Queue` called from
`run_netplay`/`TRANSITIONING` in `netplay.c`) on per-peer wall-clock timing. The
sim reads its drain state at round/continue boundaries
(`Check_LDREQ_Clear` → `fatal_error`, `game.c:466/599`), so the divergence
shows up there — and can hard-crash a peer, not just desync. Fix = add the whole
family to `gather_state`/`load_state` (see checklist) so it rolls back
atomically. This `q_ldreq` family is the tracked live instance of an unsaved
mutable sim global.

### Latent instances (defensive)

`omop_spmv_ng_table[2]`/`2[2]` (`plcnt.c`, training-write-masked today) and
`system_timer` (`work_sys.c`, RNG-reseed gated to non-network) are constant in
network mode *today* but would desync if a future mode-toggle made the sim write
them mid-match. Cheap to save (~20 bytes); do it before relying on the
invariant.

## Determinism invariants (do not break)

- **No floats in the battle sim.** Positions are `s16` fixed-point
  (`wu.xyz[axis].disp.pos`); no transcendentals. Cross-arch P2P (x86 PC vs ARM
  Vita/Android) is only safe *because* of this. If you introduce a float into a
  sim path, cross-arch determinism is gone — you'd need fixed-point or
  same-arch matchmaking (Skullgirls Mobile went full fixed-point for this).
- **RNG is table-index based and seeded to 0 in network mode.** All `Random_ix*`
  indices are in `GS_SAVE`; `Setup_Net_Random_ix` seeds them to 0 so both peers
  start identically. Never seed gameplay RNG from a machine-dependent source
  (timer, wall clock) in `MODE_NETWORK`. Effect/BG RNG streams are separated
  from gameplay RNG — keep them separate.
- **Pointers are nulled before checksumming.** `clean_state_pointers` /
  `clean_work_pointers` null every pointer field of `WORK`/`WORK_Other`/`PLW`/
  `bgw`/`task` before the checksum so heap addresses (which differ per process/
  ASLR) don't poison it. **If you add a pointer field to a saved struct, add it
  to the cleaner** — otherwise the desync detector reports false positives.

## Detection + instrumentation

- **Desync detection** (full-state djb2 checksum + GekkoNet checksum exchange +
  diverged-frame dump) is gated behind `NETPLAY_DESYNC_DETECT` (default on;
  define `=0` to compile out on perf-tight Vita). It was historically `#if DEBUG`
  only, so every Release shipped **zero** detection — "no DESYNC line in the log"
  proved nothing. Now an instrumented Release logs `DESYNC frame=N` and dumps the
  diverged state.
- **Determinism trace** (`netplay_trace.{c,h}`, env `FISTBUMP_TRACE=1|2`) writes a
  per-confirmed-frame fingerprint to `netplay.trace.log`: full-state checksum +
  RNG indices + round/continue flow + win counts + `q_ldreq` depth + (level 2)
  per-player char/action/position/vitality + game triggers. Run level ≥1 on BOTH
  peers and `diff` the two files — the first differing `cs=` line is the exact
  frame determinism broke. This is the field instrument; it needs no second
  machine to read, only to reproduce.
- **Synctest harness** (planned, audit §2.6) drives the real sim single-machine
  with forced rollbacks (GekkoNet `GekkoStressSession`) across the full roster +
  round/continue transitions — catches unsaved state offline, deterministically.
  Note GekkoNet's `limited_saving` and `desync_detection` are mutually
  exclusive; debug with `limited_saving=false`.

## Checklist — adding a mutable sim global to the save-state

1. Is it read by the sim during a frame (directly or via a rolled-back call)?
   If no → not required. If yes → continue.
2. Decide the surface: a plain game global → add to `GameState` +
   `GS_SAVE(x)` + `GS_LOAD(x)`. An AFS/effect/IO-adjacent cluster → add a
   sub-struct to `State` and gather/load it in `gather_state`/`load_state`
   (next to `EffectState`).
3. If the type contains **pointers**, null them in `clean_state_pointers`
   (and in any per-struct cleaner) so the checksum stays cross-peer-stable.
4. Ensure nothing mutates it **outside** the stepped sim. If a keepalive/pump
   must touch it (like the AFS queue), that pump is part of the bug surface —
   the field MUST roll back so re-execution converges.
5. Add it to the `FRAME`/`PLAYR` trace fingerprint if it's determinism-critical
   and human-diffable (helps the next hunt).
6. Re-run the synctest harness; confirm green across round/continue transitions.

## Save-state size note

`State` is ~465 KB (dominated by `frw[128][448]`). GekkoNet keeps its own
rollback ring of these, plus the `NETPLAY_DESYNC_DETECT` state buffer
(`STATE_BUFFER_MAX=20`). This is fine on PC; on Vita watch total RAM — a
subset checksum or `NETPLAY_DESYNC_DETECT=0` may be needed there.
