#if NETPLAY_ENABLED

#include "platform/netplay/netplay_log.h"
#include "port/paths.h"

#include <SDL3/SDL.h>
#include <stdarg.h>
#include <stdio.h>

#define NETPLAY_LOG_MAX_BYTES (512 * 1024)
#define NETPLAY_LOG_HEARTBEAT_PERIOD 60  // log a heartbeat every N sim frames

static SDL_IOStream* log_io = NULL;
static char log_path[512] = { 0 };
static char log_path_old[512] = { 0 };
static Uint64 log_bytes = 0;
static int heartbeat_counter = 0;

static void close_log(void) {
    if (log_io != NULL) {
        SDL_CloseIO(log_io);
        log_io = NULL;
    }
}

static void open_log_append(void) {
    if (log_path[0] == '\0') {
        return;
    }
    log_io = SDL_IOFromFile(log_path, "a");
    if (log_io != NULL) {
        Sint64 sz = SDL_GetIOSize(log_io);
        log_bytes = (sz > 0) ? (Uint64)sz : 0;
    } else {
        log_bytes = 0;
    }
}

static void rotate_if_needed(void) {
    if (log_bytes < NETPLAY_LOG_MAX_BYTES) {
        return;
    }
    close_log();
    // Replace old.log with current.
    SDL_RemovePath(log_path_old);
    SDL_RenamePath(log_path, log_path_old);
    log_bytes = 0;
    open_log_append();
}

void Netplay_LogInit(void) {
    const char* pref = Paths_GetPrefPath();
    if (pref == NULL) {
        return;
    }
    SDL_snprintf(log_path, sizeof(log_path), "%snetplay.log", pref);
    SDL_snprintf(log_path_old, sizeof(log_path_old), "%snetplay.old.log", pref);
    // Truncate on each launch so uploads always reflect the most recent
    // session, not stale content from before the auto-updater rolled the
    // binary forward.
    SDL_IOStream* trunc = SDL_IOFromFile(log_path, "w");
    if (trunc != NULL) {
        SDL_CloseIO(trunc);
    }
    open_log_append();
    heartbeat_counter = 0;
    Netplay_Log("SESSION", "Netplay_LogInit (build %s %s)", __DATE__, __TIME__);
}

void Netplay_LogShutdown(void) {
    if (log_io != NULL) {
        Netplay_Log("SESSION", "Netplay_LogShutdown");
        close_log();
    }
}

void Netplay_Log(const char* category, const char* fmt, ...) {
    if (log_io == NULL) {
        // Try a lazy reopen in case Init hasn't run yet (e.g., very early call).
        open_log_append();
        if (log_io == NULL) {
            return;
        }
    }

    rotate_if_needed();

    char line[1024];
    int prefix_len = SDL_snprintf(line, sizeof(line), "%llu %s ",
                                   (unsigned long long)SDL_GetTicks(),
                                   category ? category : "MISC");
    if (prefix_len <= 0 || prefix_len >= (int)sizeof(line)) {
        return;
    }

    va_list ap;
    va_start(ap, fmt);
    int body_len = SDL_vsnprintf(line + prefix_len, sizeof(line) - prefix_len, fmt, ap);
    va_end(ap);
    if (body_len < 0) {
        return;
    }
    int total = prefix_len + body_len;
    if (total >= (int)sizeof(line) - 1) {
        total = (int)sizeof(line) - 2;
    }
    line[total] = '\n';
    total += 1;

    size_t written = SDL_WriteIO(log_io, line, (size_t)total);
    log_bytes += written;
    // Batch flushes: rollback storms emit a line per frame, and an fsync per
    // line stalled the relay forward. Flush on a cadence instead, but flush the
    // forensically-important categories immediately so a crash keeps them.
    static int since_flush = 0;
    const bool urgent = category != NULL
                        && (SDL_strcmp(category, "SESSION") == 0
                            || SDL_strcmp(category, "DISCONNECT") == 0
                            || SDL_strcmp(category, "DESYNC") == 0);
    if (urgent || ++since_flush >= 60) {
        SDL_FlushIO(log_io);
        since_flush = 0;
    }
}

void Netplay_LogPerFrame(int frame, float frames_behind, int rollbacks_this_frame) {
    heartbeat_counter += 1;
    if (rollbacks_this_frame > 0) {
        Netplay_Log("ROLLBACK", "frame=%d frames_behind=%.1f rolled=%d",
                    frame, (double)frames_behind, rollbacks_this_frame);
    }
    if (heartbeat_counter >= NETPLAY_LOG_HEARTBEAT_PERIOD) {
        heartbeat_counter = 0;
        Netplay_Log("HEARTBEAT", "frame=%d frames_behind=%.1f",
                    frame, (double)frames_behind);
    }
}

#endif
