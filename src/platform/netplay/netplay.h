#if NETPLAY_ENABLED

#ifndef NETPLAY_H
#define NETPLAY_H

#include <stdbool.h>

typedef struct NetworkStats {
    int delay;
    int ping;
    int rollback;
} NetworkStats;

typedef enum NetplaySessionState {
    NETPLAY_SESSION_IDLE,
    NETPLAY_SESSION_TRANSITIONING,
    NETPLAY_SESSION_CONNECTING,
    NETPLAY_SESSION_RUNNING,
    NETPLAY_SESSION_EXITING,
    NETPLAY_SESSION_REPLAYING,  // local playback of a recorded .3sxr
} NetplaySessionState;

void Netplay_SetParams(int player, const char* ip);
void Netplay_BeginDirectP2P();
void Netplay_TickDirectP2P();
void Netplay_SetMatchmakingParams(const char* server_ip, int server_port);
void Netplay_BeginMatchmaking();
void Netplay_TickMatchmaking();
bool Netplay_IsMatchmakingPending(); // true while searching, false once matched or idle
void Netplay_FindMatch();
void Netplay_CancelMatchmaking();
void Netplay_Run();
// Load a recorded match and start local playback. Returns false (with a reason
// in Replay_GetError) if the file is invalid or from a different build.
bool Netplay_BeginReplay(const char* path);
NetplaySessionState Netplay_GetSessionState();
// Local player's slot (0/1) — for per-machine actions inside the synced
// post-match menu (e.g. only the presser saves the replay).
int Netplay_GetLocalPlayer(void);
void Netplay_HandleMenuExit();
void Netplay_GetNetworkStats(NetworkStats* stats);
float Netplay_GetFramesBehind(void);
void  Netplay_TickDiscord(void);

#endif

#endif // NETPLAY_ENABLED
