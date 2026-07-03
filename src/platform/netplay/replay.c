#if NETPLAY_ENABLED

#include "platform/netplay/replay.h"
#include "platform/netplay/netplay_log.h"
#include "port/paths.h"

#include <SDL3/SDL.h>
#include <string.h>
#include <time.h>

// Build stamp used as the playback compatibility gate. Set at compile time
// (see CMakeLists.txt); "dev" locally.
#ifndef BUILD_VERSION
#define BUILD_VERSION "dev"
#endif
#ifndef REPLAY_PROTO_VERSION
#define REPLAY_PROTO_VERSION "1.4.3"
#endif

#define REPLAY_KEEP_N     20    // auto-save ring: keep this many newest files
#define REPLAY_MIN_FRAMES 120   // shorter matches are aborts — don't save

_Static_assert(sizeof(ReplayHeader) == 128, "ReplayHeader must be exactly 128 bytes");
_Static_assert(sizeof(ReplayFrame) == 4, "ReplayFrame must be exactly 4 bytes");

// ---------------------------------------------------------------------------
//  Recording — a growable array indexed by ABSOLUTE gekko frame. Overwrite by
//  frame index means the last write per frame (always the confirmed input,
//  since a frame is never re-advanced after it leaves the rollback window) is
//  what's kept. Pure observer: never in the save-state, never read by the sim.
// ---------------------------------------------------------------------------
static ReplayFrame* rec = NULL;
static int rec_cap = 0;
static int rec_high = -1;
static bool recording = false;
static ReplayHeader rec_hdr;          // filled at begin, completed at end
static char last_saved_path[520] = { 0 };
// Frame rebasing so each GAME of a rematch series becomes its own file: the
// recorder stores at rec[frame - rec_base]. Replay_CutGame flushes the current
// game then sets rec_base to the next absolute frame, so the next game records
// from index 0. (gekko frame numbers are continuous across a rematch series.)
static int rec_base = 0;
static int rec_last_abs = -1;

static void replays_dir(char* out, size_t n) {
    // Paths_GetPrefPath already ends with a separator (see regions.c usage).
    SDL_snprintf(out, n, "%sreplays", Paths_GetPrefPath());
}

void Replay_BeginRecord(int input_delay, const char* p1_name, const char* p2_name,
                        int elo_p1, int elo_p2, u64 start_ts) {
    SDL_free(rec);
    rec = NULL;
    rec_cap = 0;
    rec_high = -1;
    rec_base = 0;
    rec_last_abs = -1;
    recording = true;

    SDL_zero(rec_hdr);
    rec_hdr.magic = REPLAY_MAGIC;
    rec_hdr.format = REPLAY_FORMAT;
    rec_hdr.input_delay = (u8)input_delay;
    rec_hdr.start_timestamp = start_ts;
    rec_hdr.elo_p1 = (elo_p1 > 0 && elo_p1 <= 0xFFFF) ? (u16)elo_p1 : 0;
    rec_hdr.elo_p2 = (elo_p2 > 0 && elo_p2 <= 0xFFFF) ? (u16)elo_p2 : 0;
    SDL_strlcpy(rec_hdr.build, BUILD_VERSION, sizeof(rec_hdr.build));
    SDL_strlcpy(rec_hdr.proto, REPLAY_PROTO_VERSION, sizeof(rec_hdr.proto));
    if (p1_name) SDL_strlcpy(rec_hdr.name_p1, p1_name, sizeof(rec_hdr.name_p1));
    if (p2_name) SDL_strlcpy(rec_hdr.name_p2, p2_name, sizeof(rec_hdr.name_p2));
    Netplay_Log("REPLAY", "begin record delay=%d p1=%s p2=%s", input_delay,
                rec_hdr.name_p1, rec_hdr.name_p2);
}

void Replay_RecordFrame(int frame, u16 p1, u16 p2) {
    if (!recording || frame < rec_base) {
        return;
    }
    rec_last_abs = frame;
    const int idx = frame - rec_base;  // rebased so each cut game starts at 0
    if (idx >= rec_cap) {
        int want = rec_cap ? rec_cap * 2 : 4096;
        while (want <= idx) {
            want *= 2;
        }
        ReplayFrame* grown = (ReplayFrame*)SDL_realloc(rec, (size_t)want * sizeof(ReplayFrame));
        if (grown == NULL) {
            return;  // OOM — stop growing; what we have still flushes
        }
        SDL_memset(grown + rec_cap, 0, (size_t)(want - rec_cap) * sizeof(ReplayFrame));
        rec = grown;
        rec_cap = want;
    }
    rec[idx].p1 = p1;
    rec[idx].p2 = p2;
    if (idx > rec_high) {
        rec_high = idx;
    }
}

bool Replay_IsRecording(void) {
    return recording;
}

const char* Replay_LastSavedPath(void) {
    return last_saved_path[0] ? last_saved_path : NULL;
}

// Delete all but the newest REPLAY_KEEP_N *.3sxr files in the replays dir.
static void prune_old_replays(void) {
    char dir[520];
    replays_dir(dir, sizeof(dir));
    int count = 0;
    char** files = SDL_GlobDirectory(dir, "*.3sxr", 0, &count);
    if (files == NULL) {
        return;
    }
    if (count > REPLAY_KEEP_N) {
        // SDL_GlobDirectory returns names sorted ascending; our filenames are
        // <utc>-... so lexicographic order == chronological. Delete the oldest.
        for (int i = 0; i < count - REPLAY_KEEP_N; i++) {
            char full[560];
            SDL_snprintf(full, sizeof(full), "%s/%s", dir, files[i]);
            SDL_RemovePath(full);
        }
    }
    SDL_free(files);
}

// Write the current recording buffer to a .3sxr with the given final metadata.
// Does NOT change recording state or free the buffer — callers decide. Returns
// true on a successful write (sets last_saved_path).
static bool write_current_recording(int char_a, int char_b, int super_a, int super_b,
                                    int color_a, int color_b, int stage,
                                    int wins_a, int wins_b) {
    const u32 frame_count = (rec_high >= 0) ? (u32)(rec_high + 1) : 0;
    if (rec == NULL || frame_count < REPLAY_MIN_FRAMES) {
        Netplay_Log("REPLAY", "skip save — too short (frames=%u)", frame_count);
        return false;
    }

    rec_hdr.frame_count = frame_count;
    rec_hdr.char_a = (u8)char_a;
    rec_hdr.char_b = (u8)char_b;
    rec_hdr.super_a = (u8)super_a;
    rec_hdr.super_b = (u8)super_b;
    rec_hdr.color_a = (u8)color_a;
    rec_hdr.color_b = (u8)color_b;
    rec_hdr.stage = (u8)stage;
    rec_hdr.wins_p1 = (u8)((wins_a >= 0 && wins_a <= 255) ? wins_a : 0);
    rec_hdr.wins_p2 = (u8)((wins_b >= 0 && wins_b <= 255) ? wins_b : 0);

    char dir[520];
    replays_dir(dir, sizeof(dir));
    SDL_CreateDirectory(dir);

    // Unique per WRITE: the wall-clock start stamp (for the display date, and to
    // keep a session's files chronologically grouped) + a monotonic ms tick at
    // write time. An in-game REMATCH keeps ONE growing recording, so without the
    // tick every save reused the same name and overwrote the previous replay.
    // Distinct ticks => each save is preserved as its own file.
    char path[600];
    SDL_snprintf(path, sizeof(path), "%s/%llu-%010llu-%s-%s.3sxr", dir,
                 (unsigned long long)rec_hdr.start_timestamp,
                 (unsigned long long)SDL_GetTicks(),
                 rec_hdr.name_p1[0] ? rec_hdr.name_p1 : "p1",
                 rec_hdr.name_p2[0] ? rec_hdr.name_p2 : "p2");

    SDL_IOStream* io = SDL_IOFromFile(path, "wb");
    if (io == NULL) {
        Netplay_Log("REPLAY", "save FAILED %s: %s", path, SDL_GetError());
        return false;
    }
    SDL_WriteIO(io, &rec_hdr, sizeof(rec_hdr));
    SDL_WriteIO(io, rec, (size_t)frame_count * sizeof(ReplayFrame));
    SDL_CloseIO(io);
    SDL_strlcpy(last_saved_path, path, sizeof(last_saved_path));
    Netplay_Log("REPLAY", "saved %s frames=%u", path, frame_count);
    prune_old_replays();
    return true;
}

void Replay_EndRecord(int char_a, int char_b, int super_a, int super_b,
                      int color_a, int color_b, int stage, int wins_a, int wins_b) {
    if (!recording) {
        return;
    }
    recording = false;
    last_saved_path[0] = '\0';
    write_current_recording(char_a, char_b, super_a, super_b, color_a, color_b,
                            stage, wins_a, wins_b);
    SDL_free(rec);
    rec = NULL;
    rec_cap = 0;
    rec_high = -1;
}

bool Replay_CutGame(int char_a, int char_b, int super_a, int super_b,
                    int color_a, int color_b, int stage, int wins_a, int wins_b) {
    // A game ended within the session — flush it as its own file, then rebase so
    // the NEXT game records from index 0 (its own file). Keeps recording.
    if (!recording) {
        return false;
    }
    const bool wrote = write_current_recording(char_a, char_b, super_a, super_b,
                                               color_a, color_b, stage, wins_a, wins_b);
    rec_base = rec_last_abs + 1;  // next absolute frame starts the next game's file
    rec_high = -1;                // fresh buffer for the next game (rec_cap kept)
    return wrote;
}

void Replay_DiscardRecord(void) {
    // Stop recording without writing (e.g. the trailing menu-only segment after
    // the last game was already cut).
    recording = false;
    SDL_free(rec);
    rec = NULL;
    rec_cap = 0;
    rec_high = -1;
}

bool Replay_SaveSnapshot(int char_a, int char_b, int super_a, int super_b,
                         int color_a, int color_b, int stage, int wins_a, int wins_b) {
    // On-demand save (post-match REPLAY button): write the current game's frames
    // so far WITHOUT ending the recording. Each save is uniquely named so nothing
    // is overwritten.
    if (!recording) {
        return false;
    }
    return write_current_recording(char_a, char_b, super_a, super_b,
                                   color_a, color_b, stage, wins_a, wins_b);
}

// ---------------------------------------------------------------------------
//  Playback
// ---------------------------------------------------------------------------
static ReplayFrame* play_buf = NULL;
static u32 play_count = 0;
static u32 play_idx = 0;
static bool playback = false;
static ReplayHeader play_hdr;
static char replay_error[128] = { 0 };

const char* Replay_GetError(void) {
    return replay_error;
}

bool Replay_LoadForPlayback(const char* path) {
    replay_error[0] = '\0';
    Replay_EndPlayback();

    SDL_IOStream* io = SDL_IOFromFile(path, "rb");
    if (io == NULL) {
        SDL_strlcpy(replay_error, "Cannot open replay file.", sizeof(replay_error));
        return false;
    }
    ReplayHeader hdr;
    if (SDL_ReadIO(io, &hdr, sizeof(hdr)) != sizeof(hdr)) {
        SDL_strlcpy(replay_error, "Replay file truncated.", sizeof(replay_error));
        SDL_CloseIO(io);
        return false;
    }
    if (hdr.magic != REPLAY_MAGIC || hdr.format != REPLAY_FORMAT) {
        SDL_strlcpy(replay_error, "Not a valid replay file.", sizeof(replay_error));
        SDL_CloseIO(io);
        return false;
    }
    // Hard build gate: playback re-simulates, so any sim/State-layout change
    // between builds can diverge — only the identical binary is guaranteed.
    if (SDL_strncmp(hdr.build, BUILD_VERSION, sizeof(hdr.build)) != 0) {
        SDL_snprintf(replay_error, sizeof(replay_error),
                     "Replay is from build %.23s (need %s).", hdr.build, BUILD_VERSION);
        SDL_CloseIO(io);
        return false;
    }
    if (hdr.frame_count == 0 || hdr.frame_count > (1u << 24)) {
        SDL_strlcpy(replay_error, "Replay frame count invalid.", sizeof(replay_error));
        SDL_CloseIO(io);
        return false;
    }

    ReplayFrame* buf = (ReplayFrame*)SDL_malloc((size_t)hdr.frame_count * sizeof(ReplayFrame));
    if (buf == NULL) {
        SDL_strlcpy(replay_error, "Out of memory loading replay.", sizeof(replay_error));
        SDL_CloseIO(io);
        return false;
    }
    size_t want = (size_t)hdr.frame_count * sizeof(ReplayFrame);
    size_t got = SDL_ReadIO(io, buf, want);
    SDL_CloseIO(io);
    if (got != want) {
        SDL_strlcpy(replay_error, "Replay body truncated.", sizeof(replay_error));
        SDL_free(buf);
        return false;
    }

    play_buf = buf;
    play_count = hdr.frame_count;
    play_idx = 0;
    play_hdr = hdr;
    playback = true;
    Netplay_Log("REPLAY", "playback load %s frames=%u", path, play_count);
    return true;
}

bool Replay_PlaybackActive(void) {
    return playback;
}

bool Replay_PlaybackNext(u16* p1, u16* p2) {
    if (!playback || play_idx >= play_count) {
        return false;
    }
    if (p1) *p1 = play_buf[play_idx].p1;
    if (p2) *p2 = play_buf[play_idx].p2;
    play_idx++;
    return true;
}

const ReplayHeader* Replay_PlaybackHeader(void) {
    return playback ? &play_hdr : NULL;
}

void Replay_EndPlayback(void) {
    SDL_free(play_buf);
    play_buf = NULL;
    play_count = 0;
    play_idx = 0;
    playback = false;
}

// ---------------------------------------------------------------------------
//  Browser listing (newest first)
// ---------------------------------------------------------------------------
int Replay_ListDir(ReplayEntry* out, int max) {
    if (out == NULL || max <= 0) {
        return 0;
    }
    char dir[520];
    replays_dir(dir, sizeof(dir));
    int count = 0;
    char** files = SDL_GlobDirectory(dir, "*.3sxr", 0, &count);
    if (files == NULL || count <= 0) {
        if (files) SDL_free(files);
        return 0;
    }
    int n = 0;
    // Glob is ascending (== chronological for <utc>- names); walk newest first.
    for (int i = count - 1; i >= 0 && n < max; i--) {
        ReplayEntry* e = &out[n];
        SDL_snprintf(e->path, sizeof(e->path), "%s/%s", dir, files[i]);
        e->label[0] = '\0';
        e->detail[0] = '\0';
        // Read just the header for a friendly label + detail line.
        SDL_IOStream* io = SDL_IOFromFile(e->path, "rb");
        if (io != NULL) {
            ReplayHeader h;
            if (SDL_ReadIO(io, &h, sizeof(h)) == sizeof(h) && h.magic == REPLAY_MAGIC) {
                char date[16] = "--/--/----";
                if (h.start_timestamp != 0) {
                    time_t ts = (time_t)h.start_timestamp;
                    struct tm* lt = localtime(&ts);
                    if (lt != NULL) {
                        SDL_snprintf(date, sizeof(date), "%02d/%02d/%04d",
                                     lt->tm_mday, lt->tm_mon + 1, lt->tm_year + 1900);
                    }
                }
                // "P1 VS P2 (2W - 0L) 01/07/2026" — score is p1's view;
                // the higher count is the winner.
                SDL_snprintf(e->label, sizeof(e->label), "%s VS %s (%uW - %uL) %s",
                             h.name_p1[0] ? h.name_p1 : "p1",
                             h.name_p2[0] ? h.name_p2 : "p2",
                             (unsigned)h.wins_p1, (unsigned)h.wins_p2, date);
                if (h.elo_p1 > 0 || h.elo_p2 > 0) {
                    SDL_snprintf(e->detail, sizeof(e->detail),
                                 "ELO %u vs %u  |  3SX %.23s  proto %.7s",
                                 (unsigned)h.elo_p1, (unsigned)h.elo_p2, h.build, h.proto);
                } else {
                    SDL_snprintf(e->detail, sizeof(e->detail),
                                 "3SX %.23s  proto %.7s", h.build, h.proto);
                }
            }
            SDL_CloseIO(io);
        }
        if (e->label[0] == '\0') {
            SDL_strlcpy(e->label, files[i], sizeof(e->label));
        }
        n++;
    }
    SDL_free(files);
    return n;
}

#endif // NETPLAY_ENABLED
