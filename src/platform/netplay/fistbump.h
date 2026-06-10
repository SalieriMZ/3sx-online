#if NETPLAY_ENABLED

#ifndef FISTBUMP_H
#define FISTBUMP_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    FISTBUMP_IDLE,
    FISTBUMP_CONNECTING,
    FISTBUMP_SENDING_TOKEN,
    FISTBUMP_LOGGING_IN,
    FISTBUMP_AWAITING_LOGIN,
    FISTBUMP_AWAITING_CREDENTIALS,  // no saved token — overlay UI must REGISTER or LOGIN
    FISTBUMP_AWAITING_MATCH,
    FISTBUMP_MATCHED,
    FISTBUMP_SENDING_UDP,
    FISTBUMP_GAME_START,
    FISTBUMP_ERROR,
} FistbumpState;

typedef enum {
    FISTBUMP_CONN_IDLE,
    FISTBUMP_CONN_RESOLVING_DNS,
    FISTBUMP_CONN_CONNECTING_TCP,
    FISTBUMP_CONN_CONNECTED,
    FISTBUMP_CONN_ERROR,
} FistbumpConnectState;

typedef struct NET_DatagramSocket NET_DatagramSocket;

typedef struct {
    char match_id[37];
    int player; // 1 or 2
    char opponent_name[64];
    char ip[64];     // remote peer IP string
    int remote_port; // remote peer game port (parsed from "ip:port")
    // Phase-1 relay (server-side routing):
    char relay_ip[64];
    int  relay_port;
    bool use_relay;
} MatchResult;

typedef struct {
    char code[9];
    char activate_url[128];
} DAG;

typedef struct {
    char token[1024];
    int expiry;
} JWT;

typedef struct {
    char username[64];
} Fistbump_Profile;

void Fistbump_Start(const char* server_ip, int tcp_port, int udp_port, const char* pref_path);
void Fistbump_Connect();
// Overlay-login auth — call after Connect() lands in AWAITING_CREDENTIALS.
// Transitions to LOGGING_IN; success arrives via PROFILE / TOKEN messages.
void Fistbump_Register(const char* username, const char* password);
void Fistbump_LoginDirect(const char* username, const char* password);
// Reconnect to a different matchmaking server (region picker). Tears down
// + re-Starts on the same prefpath. Must be user-driven.
void Fistbump_SetServer(const char* server_ip, int tcp_port, int udp_port);
// SET force_relay wire cmd — advertises CGNAT mitigation preference for the
// session. Safe to call mid-session; server forwards to peer at MATCH time.
void Fistbump_SetForceRelay(int v);
// Last REJECT reason from server (empty when none). Cleared on next auth try.
const char* Fistbump_GetLastError(void);
void Fistbump_ClearError(void);
// Username from current PROFILE response (empty if not logged in).
const char* Fistbump_GetUsername(void);
void Fistbump_Queue();                       // legacy: casual
void Fistbump_QueueMode(const char* mode);   // "casual" | "ranked"
// Edge-detected LOADING signal sender. Pass true while the local CPS3 AFS
// queue is draining; the wire sees "LOADING 1" / "LOADING 0" only on
// transitions. Peer sets fistbump_peer_loading so the netplay layer can pause
// gekko advance during a remote stall.
void Fistbump_TickLoadingSignal(bool local_busy);
extern bool fistbump_peer_loading;
void Fistbump_CancelQueue();
void Fistbump_RoomCreate(const char* name);
void Fistbump_RoomJoin(const char* code);
void Fistbump_RoomLeave(void);
// 8-person room with host-picked slots. Slot is 'A' or 'B'.
void Fistbump_RoomSlot(char slot);
void Fistbump_RoomUnslot(void);
// Host-only — server rejects if not host. Dispatches match between slot_a
// and slot_b occupants. No-op when match already in progress or slots empty.
void Fistbump_RoomStart(void);
// Host-only. key in {best_of, timer, damage}.
void Fistbump_RoomSettings(const char* key, int value);
void Fistbump_SendResult(int my_wins, int opp_wins);

// One-shot TCP connect timer (no auth). Returns RTT in ms on success, < 0 on
// failure. Synchronous — caller runs on background thread. Used by region
// picker UI to surface latency to each candidate matchmaking server.
float Fistbump_PingHost(const char* host, int tcp_port, float timeout_sec);

#define FISTBUMP_ROOM_MAX_MEMBERS 8

typedef struct {
    char code[16];
    char name[64];
    char host_name[16];                                  // username of host
    char slot_a_name[16];                                // "-" if empty
    char slot_b_name[16];
    char member_names[FISTBUMP_ROOM_MAX_MEMBERS][16];    // usernames
    int  members;                                         // count of valid member_names
    int  settings_best_of;                                // rounds-to-win
    int  settings_timer;                                  // round timer seconds
    int  settings_damage;                                 // 0..2
    bool peer_present;                                    // true if room has >=2 members (legacy)
    bool match_in_progress;                               // current_match_id != '-'
    bool is_host;                                         // local user IS host_name
    bool is_slot_a;                                       // local user IS slot_a_name
    bool is_slot_b;                                       // local user IS slot_b_name
} Fistbump_Room;

const Fistbump_Room* Fistbump_GetRoom(void);  // returns NULL if not in a room
void Fistbump_HandleROOM(const char* line);
void Fistbump_AcceptMatch();
void Fistbump_DeclineMatch();
void Fistbump_Run();
FistbumpState Fistbump_GetState();
FistbumpConnectState Fistbump_GetConnectState();
const MatchResult* Fistbump_GetResult();  // valid when MATCHED
NET_DatagramSocket* Fistbump_GetSocket(); // ephemeral UDP socket, valid when MATCHED
DAG Fistbump_GetDAG();
bool Fistbump_IsLoggedIn();
void Fistbump_Logout();
void Fistbump_Reset();
// Partial reset: clear match-specific state (match_result, ephemeral UDP
// socket, state=IDLE) but keep tcp_sock, profile, and room context alive.
// Used post-match so the user stays in the room when returning to Network.
void Fistbump_EndMatch();

void Fistbump_HandleCHAT(const char* line);
bool Fistbump_SendChatRaw(const char* buf, size_t len);

#endif

#endif // NETPLAY_ENABLED
