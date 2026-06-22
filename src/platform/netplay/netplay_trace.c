#if NETPLAY_ENABLED

#include "platform/netplay/netplay_trace.h"
#include "port/paths.h"

#include <SDL3/SDL.h>
#include <stdarg.h>

// Heavy trace gets a much larger budget than the event log (which is ~512KB,
// uploadable). At level 1 a fingerprint line is ~150B/frame * 60fps ~= 9KB/s,
// so 16MB * 2 (current + rolled) is roughly half an hour of continuous play —
// enough to capture a desync that surfaces "after a while".
#define NETPLAY_TRACE_MAX_BYTES (16 * 1024 * 1024)

int netplay_trace_level = 0;

static SDL_IOStream* trace_io = NULL;
static char trace_path[512] = { 0 };
static char trace_path_old[512] = { 0 };
static Uint64 trace_bytes = 0;

static void trace_close(void) {
    if (trace_io != NULL) {
        SDL_CloseIO(trace_io);
        trace_io = NULL;
    }
}

static void trace_open_append(void) {
    if (trace_path[0] == '\0') {
        return;
    }
    trace_io = SDL_IOFromFile(trace_path, "a");
    if (trace_io != NULL) {
        Sint64 sz = SDL_GetIOSize(trace_io);
        trace_bytes = (sz > 0) ? (Uint64)sz : 0;
    } else {
        trace_bytes = 0;
    }
}

static void trace_rotate_if_needed(void) {
    if (trace_bytes < NETPLAY_TRACE_MAX_BYTES) {
        return;
    }
    trace_close();
    SDL_RemovePath(trace_path_old);
    SDL_RenamePath(trace_path, trace_path_old);
    trace_bytes = 0;
    trace_open_append();
}

static void trace_write_line(const char* category, const char* body) {
    if (trace_io == NULL) {
        return;
    }
    trace_rotate_if_needed();
    if (trace_io == NULL) {
        return;
    }

    char line[1024];
    int total = SDL_snprintf(line, sizeof(line), "%llu %s %s\n",
                             (unsigned long long)SDL_GetTicks(),
                             category ? category : "TRACE",
                             body ? body : "");
    if (total <= 0) {
        return;
    }
    if (total >= (int)sizeof(line)) {
        total = (int)sizeof(line) - 1;
        line[total - 1] = '\n';
    }
    size_t written = SDL_WriteIO(trace_io, line, (size_t)total);
    trace_bytes += written;
    SDL_FlushIO(trace_io);
}

void Netplay_TraceInit(void) {
    const char* env = SDL_getenv("FISTBUMP_TRACE");
    int level = 0;
    if (env != NULL && env[0] != '\0') {
        level = SDL_atoi(env);
        if (level < 0) level = 0;
        if (level > 2) level = 2;
    }
    netplay_trace_level = level;

    if (level <= 0) {
        // Stay completely dormant — don't even create the file.
        return;
    }

    if (trace_path[0] == '\0') {
        const char* pref = Paths_GetPrefPath();
        if (pref == NULL) {
            netplay_trace_level = 0;
            return;
        }
        SDL_snprintf(trace_path, sizeof(trace_path), "%snetplay.trace.log", pref);
        SDL_snprintf(trace_path_old, sizeof(trace_path_old), "%snetplay.trace.old.log", pref);

        // Truncate once per process launch (this block only runs the first time)
        // so each run's trace stands alone — appending across runs made the
        // determinism diff ambiguous.
        SDL_IOStream* trunc = SDL_IOFromFile(trace_path, "w");
        if (trunc != NULL) {
            SDL_CloseIO(trunc);
        }
    }

    if (trace_io == NULL) {
        trace_open_append();
    }
    trace_write_line("TRACE", "session start");
}

void Netplay_TraceShutdown(void) {
    if (trace_io != NULL) {
        trace_write_line("TRACE", "session end");
        trace_close();
    }
}

int Netplay_TraceLevel(void) {
    return netplay_trace_level;
}

void Netplay_TraceFrame(const NetplayTraceFrame* tf) {
    if (netplay_trace_level < 1 || tf == NULL || trace_io == NULL) {
        return;
    }

    char body[512];

    // Line 1: determinism fingerprint (level >= 1). Compact, stable field order
    // so two peers' logs diff cleanly.
    SDL_snprintf(body, sizeof(body),
        "f=%d cs=0x%08x gs=0x%08x es=0x%08x ldcs=0x%08x rb=%d fb=%.1f round=%d wins=%d/%d "
        "gno=%d,%d,%d,%d cno=%d,%d,%d,%d rng=%d,%d,%d,%d ld=%d afs=%d",
        tf->frame, tf->checksum, tf->cs_gs, tf->cs_es, tf->cs_ld,
        tf->rollbacks, (double)tf->frames_behind,
        tf->round_num, tf->wins[0], tf->wins[1],
        tf->gno[0], tf->gno[1], tf->gno[2], tf->gno[3],
        tf->cno[0], tf->cno[1], tf->cno[2], tf->cno[3],
        tf->rng_ix16, tf->rng_ix32, tf->rng_ix16_ex, tf->rng_ix32_ex,
        tf->ld_depth, tf->afs_io);
    trace_write_line("FRAME", body);

    if (netplay_trace_level >= 2) {
        // Line 2: per-player detail (char / action / position / vitality).
        SDL_snprintf(body, sizeof(body),
            "f=%d p0{c=%d act=%d ci=%d x=%d y=%d vit=%d} "
            "p1{c=%d act=%d ci=%d x=%d y=%d vit=%d}",
            tf->frame,
            tf->p_char[0], tf->p_act[0], tf->p_charidx[0], tf->p_x[0], tf->p_y[0], tf->p_vit[0],
            tf->p_char[1], tf->p_act[1], tf->p_charidx[1], tf->p_x[1], tf->p_y[1], tf->p_vit[1]);
        trace_write_line("PLAYR", body);
    }
}

void Netplay_TraceTrigger(const char* tag, const char* fmt, ...) {
    if (netplay_trace_level < 2 || trace_io == NULL) {
        return;
    }
    char body[512];
    char msg[480];
    va_list ap;
    va_start(ap, fmt);
    SDL_vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    SDL_snprintf(body, sizeof(body), "%s %s", tag ? tag : "?", msg);
    trace_write_line("TRIG", body);
}

#endif
