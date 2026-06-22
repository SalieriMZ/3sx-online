#if NETPLAY_ENABLED

#ifndef NETPLAY_TRACE_H
#define NETPLAY_TRACE_H

#include "types.h"

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdint.h>

// Intense VS-online determinism trace. This is the HEAVY, structured channel,
// kept separate from netplay_log (the lightweight, uploadable event log). The
// trace writes its own rotating file <PrefPath>/netplay.trace.log with a large
// budget so a desync that only shows "after a while" is still captured.
//
// Runtime-gated by env FISTBUMP_TRACE:
//   0 (default) — off, ~zero cost (one bool test per frame, no file opened)
//   1           — per-confirmed-frame fingerprint (checksum + determinism scalars)
//   2           — also per-player detail (char/action/pos/vitality) + game triggers
//
// Two peers running level >= 1 produce logs whose first differing `cs=` line is
// the exact frame determinism broke; `diff` the two netplay.trace.log files.

// Determinism fingerprint for one confirmed/saved frame. POD so the trace
// module needs no game headers; netplay.c fills it from the live globals.
typedef struct NetplayTraceFrame {
    int      frame;
    uint32_t checksum;       // full-state djb2; 0 when detection compiled out
    uint32_t cs_gs;          // sub-checksum: GameState surface only
    uint32_t cs_es;          // sub-checksum: EffectState (effect pool) only
    uint32_t cs_ld;          // sub-checksum: q_ldreq load-queue snapshot only
    int      rollbacks;      // 1 if this saved frame was a rollback re-sim
    float    frames_behind;

    int round_num;
    int wins[2];
    int gno[4];
    int cno[4];

    int rng_ix16, rng_ix32, rng_ix16_ex, rng_ix32_ex;

    int ld_depth;            // busy entries in q_ldreq (suspected desync source)
    int afs_io;              // afs_io_in_progress

    // per player
    int p_char[2];           // My_char
    int p_act[2];            // wu.cg_number (animation / action)
    int p_charidx[2];        // wu.char_index
    int p_x[2], p_y[2];      // wu.xyz[0/1].disp.pos (s16 fixed-point integer part)
    int p_vit[2];            // wu.vital_new
} NetplayTraceFrame;

extern int netplay_trace_level;   // 0/1/2 — read by the inline gate below

void Netplay_TraceInit(void);     // read env, open file, set level (once per match)
void Netplay_TraceShutdown(void);
int  Netplay_TraceLevel(void);

static inline bool Netplay_TraceOn(void) {
    return netplay_trace_level > 0;
}

void Netplay_TraceFrame(const NetplayTraceFrame* tf);

// Discrete game trigger (level >= 2). tag e.g. "ROUND", "KO", "SUPER", "LDREQ".
void Netplay_TraceTrigger(const char* tag, SDL_PRINTF_FORMAT_STRING const char* fmt, ...) SDL_PRINTF_VARARG_FUNC(2);

#endif

#endif
