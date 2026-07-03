#if NETPLAY_ENABLED

#ifndef REPLAY_H
#define REPLAY_H

#include "types.h"
#include <stdbool.h>
#include <stddef.h>

// Match replay format. A replay is the confirmed per-frame input stream of both
// players plus the deterministic-start metadata; playback re-simulates it on the
// SAME build (byte-identical), so no game state is serialized — only inputs.
// See docs and src/platform/netplay/replay.c for the design rationale.

#define REPLAY_MAGIC   0x52585333u  // "3SXR" little-endian
#define REPLAY_FORMAT  1

// One body entry: the (inputs[0], inputs[1]) u16 pair advance_game consumes.
typedef struct ReplayFrame {
    u16 p1;
    u16 p2;
} ReplayFrame;

// 128-byte fixed header (packed, little-endian on all supported targets).
typedef struct ReplayHeader {
    u32  magic;
    u16  format;
    u16  flags;
    char build[24];   // BUILD_VERSION — exact-match gate for playback
    char proto[8];    // protocol version string (informational)
    u8   char_a, char_b;
    u8   super_a, super_b;
    u8   color_a, color_b;
    u8   stage;
    u8   input_delay;
    u32  frame_count;
    u16  checksum_stride;  // 0 = no checksum trailer
    u16  _pad0;
    u64  start_timestamp;
    char name_p1[24];
    char name_p2[24];
    u16  elo_p1;      // each player's ELO at match time (0 = unknown)
    u16  elo_p2;
    u8   wins_p1;     // final series score (game wins) — decides the winner
    u8   wins_p2;
    u8   reserved[10];
} ReplayHeader;

// ---- Recording (write-only observer; safe under rollback) ----
// Begin capturing a match. names may be NULL; elos 0 when unknown (direct P2P /
// older server). Caller supplies the UTC stamp.
void Replay_BeginRecord(int input_delay, const char* p1_name, const char* p2_name,
                        int elo_p1, int elo_p2, u64 start_ts);
// Store the confirmed inputs for an absolute gekko frame (overwrite-by-frame:
// the last write per frame is the confirmed value).
void Replay_RecordFrame(int frame, u16 p1, u16 p2);
bool Replay_IsRecording(void);
// Flush the recording to <prefpath>/replays/<ts>.3sxr with the final match
// metadata, prune to the newest REPLAY_KEEP_N files, and free the buffer. A
// too-short match (aborted) is discarded. Safe to call when not recording.
void Replay_EndRecord(int char_a, int char_b, int super_a, int super_b,
                      int color_a, int color_b, int stage, int wins_a, int wins_b);
// Save what's recorded so far WITHOUT ending the recording (post-match REPLAY
// button). Returns true if a file was written. No-op if not recording.
bool Replay_SaveSnapshot(int char_a, int char_b, int super_a, int super_b,
                         int color_a, int color_b, int stage, int wins_a, int wins_b);
// Flush the current game as its own file and rebase for the next game (called at
// each game boundary in a rematch series so each game is a separate replay).
bool Replay_CutGame(int char_a, int char_b, int super_a, int super_b,
                    int color_a, int color_b, int stage, int wins_a, int wins_b);
// Stop recording without writing (trailing menu-only segment after the last cut).
void Replay_DiscardRecord(void);
// True if the most recent EndRecord actually wrote a file (for the VS_Result
// REPLAY row + auto-save). Returns the saved path or NULL.
const char* Replay_LastSavedPath(void);

// ---- Playback ----
// Load + validate a .3sxr for playback. Rejects a bad magic/format or a build
// mismatch (playback re-simulates, so only the identical binary is safe).
// Returns false and sets an error string (Replay_GetError) on failure.
bool Replay_LoadForPlayback(const char* path);
bool Replay_PlaybackActive(void);
// Pop the next recorded frame's inputs. Returns false at end of stream.
bool Replay_PlaybackNext(u16* p1, u16* p2);
const ReplayHeader* Replay_PlaybackHeader(void);
void Replay_EndPlayback(void);
const char* Replay_GetError(void);

// ---- Browser ----
typedef struct ReplayEntry {
    char path[520];
    char label[96];   // "<p1> VS <p2> (xW - yL) dd/mm/yyyy"
    char detail[96];  // "ELO 1520 vs 1480  |  3SX 1.9.0  proto 1.4.3"
} ReplayEntry;

// Scan <prefpath>/replays/ newest-first. Returns count written (<= max).
int Replay_ListDir(ReplayEntry* out, int max);

#endif // REPLAY_H

#endif // NETPLAY_ENABLED
