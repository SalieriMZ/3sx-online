#if NETPLAY_ENABLED

#include "platform/netplay/fistbump.h"
#include "platform/netplay/chat.h"
#include "platform/netplay/netplay_log.h"
#include "port/config/config.h"
#include "port/creds.h"
#include "port/io/local_ip.h"
#include "args.h"

#include <SDL3/SDL.h>
#include <SDL3_net/SDL_net.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define FISTBUMP_CLIENT_VERSION "1.3.0"

static FistbumpState state = FISTBUMP_IDLE;
static FistbumpConnectState connect_state = FISTBUMP_CONN_IDLE;

static NET_Address* server_addr = NULL;
static NET_StreamSocket* tcp_sock = NULL;
static NET_DatagramSocket* udp_sock = NULL;

static int saved_tcp_port = 0;
static int saved_udp_port = 0;

static char id_buf[8]; // 7-char ID + null
static char line_buf[1024];
static int line_len = 0;
static int udp_retry_timer = 0;

static const char* base_path = NULL;

static DAG dag;
static JWT refresh_token;
static Fistbump_Profile profile;
static MatchResult match_result;
static Fistbump_Room current_room;
static bool in_room = false;
static bool result_sent = false;
static char last_error[256] = { 0 };

static void SaveToken(const JWT* jwt) {
    if (base_path == NULL || jwt == NULL) {
        return;
    }

    char path[512];
    SDL_snprintf(path, sizeof(path), "%s/token", base_path);

    SDL_IOStream* stream = SDL_IOFromFile(path, "w");
    if (!stream) {
        return;
    }

    SDL_IOprintf(stream, "%s\n%d\n", jwt->token, jwt->expiry);
    SDL_CloseIO(stream);
}

static bool LoadToken(JWT* jwt) {
    if (base_path == NULL || jwt == NULL) {
        return false;
    }

    char path[512];
    SDL_snprintf(path, sizeof(path), "%s/token", base_path);

    SDL_IOStream* stream = SDL_IOFromFile(path, "r");
    if (!stream) {
        return false;
    }

    char buf[512];
    size_t total = SDL_ReadIO(stream, buf, sizeof(buf) - 1);
    SDL_CloseIO(stream);
    if (total == 0) {
        return false;
    }
    buf[total] = '\0';

    char* nl = strpbrk(buf, "\r\n");
    if (nl == NULL) {
        return false;
    }
    *nl = '\0';
    SDL_strlcpy(jwt->token, buf, sizeof(jwt->token));

    char* expiry_str = nl + 1;
    while (*expiry_str == '\r' || *expiry_str == '\n') {
        expiry_str++;
    }

    char* endptr = NULL;
    long expiry = strtol(expiry_str, &endptr, 10);
    if (endptr == expiry_str) {
        return false;
    }
    jwt->expiry = (time_t)expiry;

    time_t now = time(NULL);
    if (jwt->expiry <= now) {
        remove(path);
        return false;
    }

    return true;
}

static void DeleteToken() {
    if (!base_path) {
        return;
    }

    char path[512];
    SDL_snprintf(path, sizeof(path), "%s/token", base_path);

    remove(path);
}

static bool pop_line(char* out, int out_size) {
    for (int i = 0; i < line_len; i++) {
        if (line_buf[i] == '\n') {

            int len = i;
            if (len >= out_size)
                len = out_size - 1;

            memcpy(out, line_buf, len);
            out[len] = '\0';

            int remaining = line_len - (i + 1);
            memmove(line_buf, line_buf + i + 1, remaining);
            line_len = remaining;

            return true;
        }
    }
    return false;
}

static void read_into_line_buf() {
    int space = (int)sizeof(line_buf) - line_len - 1;

    if (space <= 0) {
        return;
    }

    int n = NET_ReadFromStreamSocket(tcp_sock, line_buf + line_len, space);

    if (n > 0) {
        line_len += n;
    }
}

void Fistbump_Start(const char* server_ip, int tcp_port, int udp_port, const char* pref_path) {
    NET_Init();
    Chat_Init();

    // Drop any prior socket so we don't leak the sid (the user would lose room membership).
    if (tcp_sock != NULL) {
        NET_DestroyStreamSocket(tcp_sock);
        tcp_sock = NULL;
    }
    if (server_addr != NULL) {
        NET_UnrefAddress(server_addr);
        server_addr = NULL;
    }

    saved_tcp_port = tcp_port;
    saved_udp_port = udp_port;

    SDL_zeroa(id_buf);
    SDL_zeroa(line_buf);
    line_len = 0;
    udp_retry_timer = 0;
    SDL_zero(match_result);

    base_path = pref_path;

    server_addr = NET_ResolveHostname(server_ip);
    state = FISTBUMP_CONNECTING;
    connect_state = FISTBUMP_CONN_RESOLVING_DNS;
}

void Fistbump_Connect() {
    switch (connect_state) {
    case FISTBUMP_CONN_IDLE:
        break;

    case FISTBUMP_CONN_RESOLVING_DNS:
        switch (NET_GetAddressStatus(server_addr)) {
        case NET_SUCCESS:
            tcp_sock = NET_CreateClient(server_addr, (Uint16)saved_tcp_port);
            if (tcp_sock == NULL) {
                SDL_Log("Fistbump: failed to create TCP client: %s\n", SDL_GetError());
                connect_state = FISTBUMP_CONN_ERROR;
            } else {
                connect_state = FISTBUMP_CONN_CONNECTING_TCP;
            }
            break;

        case NET_FAILURE:
            SDL_Log("Fistbump: DNS resolution failed: %s\n", SDL_GetError());
            state = FISTBUMP_ERROR;
            break;

        case NET_WAITING:
            break;
        }
        break;

    case FISTBUMP_CONN_CONNECTING_TCP:
        switch (NET_GetConnectionStatus(tcp_sock)) {
        case NET_SUCCESS:
            connect_state = FISTBUMP_CONN_CONNECTED;
            break;

        case NET_FAILURE:
            SDL_Log("Fistbump: TCP connection failed: %s\n", SDL_GetError());
            connect_state = FISTBUMP_CONN_ERROR;
            break;

        case NET_WAITING:
            break;
        }
        break;

    case FISTBUMP_CONN_CONNECTED:
        break;

    case FISTBUMP_CONN_ERROR:
        state = FISTBUMP_ERROR;
        break;
    }
}

void Fistbump_Login() {
    if (LoadToken(&refresh_token)) {
        state = FISTBUMP_LOGGING_IN;
        char buf[1100];
        SDL_snprintf(buf, sizeof(buf), "REFRESH %s %s\n", refresh_token.token, FISTBUMP_CLIENT_VERSION);
        NET_WriteToStreamSocket(tcp_sock, buf, SDL_strlen(buf));
    } else {
        // No saved token — wait for the UI to REGISTER or LOGIN.
        state = FISTBUMP_AWAITING_CREDENTIALS;
    }
}

void Fistbump_Register(const char* username, const char* password) {
    if (tcp_sock == NULL || username == NULL || password == NULL) {
        return;
    }
    char buf[256];
    SDL_snprintf(buf, sizeof(buf), "REGISTER %s %s %s\n", username, password, FISTBUMP_CLIENT_VERSION);
    NET_WriteToStreamSocket(tcp_sock, buf, (int)SDL_strlen(buf));
    last_error[0] = '\0';
    state = FISTBUMP_LOGGING_IN;
    Netplay_Log("AUTH", "register attempt user=%s", username);
}

void Fistbump_LoginDirect(const char* username, const char* password) {
    if (tcp_sock == NULL || username == NULL || password == NULL) {
        return;
    }
    char buf[256];
    SDL_snprintf(buf, sizeof(buf), "LOGIN %s %s %s\n", username, password, FISTBUMP_CLIENT_VERSION);
    NET_WriteToStreamSocket(tcp_sock, buf, (int)SDL_strlen(buf));
    last_error[0] = '\0';
    state = FISTBUMP_LOGGING_IN;
    Netplay_Log("AUTH", "login attempt user=%s", username);
}

void Fistbump_SetServer(const char* server_ip, int tcp_port, int udp_port) {
    const char* saved_base = base_path;
    Fistbump_Reset();
    Fistbump_Start(server_ip, tcp_port, udp_port, saved_base);
}

void Fistbump_SetForceRelay(int v) {
    if (tcp_sock == NULL) {
        return;
    }
    char buf[32];
    SDL_snprintf(buf, sizeof(buf), "SET force_relay %d\n", v ? 1 : 0);
    NET_WriteToStreamSocket(tcp_sock, buf, (int)SDL_strlen(buf));
    SDL_Log("Fistbump: force_relay=%d advertised mid-session", v ? 1 : 0);
}

const char* Fistbump_GetLastError(void) {
    return last_error;
}

void Fistbump_ClearError(void) {
    last_error[0] = '\0';
}

const char* Fistbump_GetUsername(void) {
    return profile.username;
}

void Fistbump_Queue() {
    Fistbump_QueueMode("casual");
}

void Fistbump_QueueMode(const char* mode) {
    state = FISTBUMP_AWAITING_MATCH;

    char buf[128];
    if (mode != NULL && mode[0] != '\0') {
        SDL_snprintf(buf, sizeof(buf), "QUEUE add %s\n", mode);
    } else {
        SDL_snprintf(buf, sizeof(buf), "QUEUE add\n");
    }
    NET_WriteToStreamSocket(tcp_sock, buf, SDL_strlen(buf));
    Netplay_Log("QUEUE", "add mode=%s", (mode && mode[0]) ? mode : "casual");
}

void Fistbump_RoomCreate(const char* name) {
    char buf[128];
    SDL_snprintf(buf, sizeof(buf), "ROOM CREATE %s\n",
                 (name != NULL && name[0] != '\0') ? name : "Room");
    NET_WriteToStreamSocket(tcp_sock, buf, SDL_strlen(buf));
}

void Fistbump_RoomJoin(const char* code) {
    if (code == NULL || code[0] == '\0') return;
    char buf[128];
    SDL_snprintf(buf, sizeof(buf), "ROOM JOIN %s\n", code);
    NET_WriteToStreamSocket(tcp_sock, buf, SDL_strlen(buf));
}

void Fistbump_RoomLeave(void) {
    const char* cmd = "ROOM LEAVE\n";
    NET_WriteToStreamSocket(tcp_sock, cmd, (int)SDL_strlen(cmd));
}

void Fistbump_RoomSlot(char slot) {
    if (tcp_sock == NULL || (slot != 'A' && slot != 'B')) return;
    char buf[24];
    int len = SDL_snprintf(buf, sizeof(buf), "ROOM SLOT %c\n", slot);
    NET_WriteToStreamSocket(tcp_sock, buf, len);
}

void Fistbump_RoomUnslot(void) {
    if (tcp_sock == NULL) return;
    const char* cmd = "ROOM UNSLOT\n";
    NET_WriteToStreamSocket(tcp_sock, cmd, (int)SDL_strlen(cmd));
}

void Fistbump_RoomStart(void) {
    if (tcp_sock == NULL) return;
    const char* cmd = "ROOM START\n";
    NET_WriteToStreamSocket(tcp_sock, cmd, (int)SDL_strlen(cmd));
    Netplay_Log("ROOM", "host pressed START");
}

void Fistbump_RoomSettings(const char* key, int value) {
    if (tcp_sock == NULL || key == NULL) return;
    char buf[64];
    int len = SDL_snprintf(buf, sizeof(buf), "ROOM SETTINGS %s %d\n", key, value);
    NET_WriteToStreamSocket(tcp_sock, buf, len);
}

void Fistbump_CancelQueue() {
    state = FISTBUMP_IDLE;

    char buf[128];
    SDL_snprintf(buf, sizeof(buf), "QUEUE remove\n");
    NET_WriteToStreamSocket(tcp_sock, buf, SDL_strlen(buf));
    Netplay_Log("QUEUE", "cancel");
}

void Fistbump_BeginUDP() {
    state = FISTBUMP_SENDING_UDP;
    udp_retry_timer = 0;
}

void Fistbump_SendUDP() {
    if (udp_sock == NULL) {
        udp_sock = NET_CreateDatagramSocket(NULL, 0);

        if (udp_sock == NULL) {
            SDL_Log("Fistbump: failed to create UDP socket: %s\n", SDL_GetError());
            state = FISTBUMP_ERROR;
            return;
        }
    }

    if (udp_retry_timer <= 0) {
        char buf[128];
        SDL_snprintf(buf, sizeof(buf), "%s %s", id_buf, match_result.match_id);
        NET_SendDatagram(udp_sock, server_addr, (Uint16)saved_udp_port, buf, SDL_strlen(buf));
        udp_retry_timer = 30; // retransmit every ~0.5 seconds
    }

    udp_retry_timer--;
}

void Fistbump_AcceptMatch() {
    Netplay_Log("MATCH", "accept id=%s", match_result.match_id);
    Fistbump_BeginUDP();
}

void Fistbump_DeclineMatch() {
    if (tcp_sock == NULL || match_result.match_id[0] == '\0') {
        return;
    }
    Netplay_Log("MATCH", "decline id=%s", match_result.match_id);
    char buf[128];
    SDL_snprintf(buf, sizeof(buf), "DECLINE %s\n", match_result.match_id);
    NET_WriteToStreamSocket(tcp_sock, buf, (int)SDL_strlen(buf));
    // Server's CANCEL only goes to the OTHER peer; reset locally too.
    SDL_zero(match_result);
    result_sent = false;
    state = FISTBUMP_IDLE;
}

void Fistbump_SendResult(int my_wins, int opp_wins) {
    if (tcp_sock == NULL || match_result.match_id[0] == '\0' || result_sent) {
        return;
    }
    char buf[128];
    SDL_snprintf(buf, sizeof(buf), "RESULT %s %d %d\n",
                 match_result.match_id, my_wins, opp_wins);
    NET_WriteToStreamSocket(tcp_sock, buf, (int)SDL_strlen(buf));
    SDL_Log("Fistbump: sent RESULT match=%s wins=%d/%d", match_result.match_id, my_wins, opp_wins);
    Netplay_Log("MATCH", "result id=%s my=%d opp=%d", match_result.match_id, my_wins, opp_wins);
    result_sent = true;
}

float Fistbump_PingHost(const char* host, int tcp_port, float timeout_sec) {
    if (host == NULL || tcp_port <= 0) {
        return -1.0f;
    }

    NET_Init();
    NET_Address* addr = NET_ResolveHostname(host);
    if (addr == NULL) {
        return -1.0f;
    }

    Uint64 deadline_ms = SDL_GetTicks() + (Uint64)(timeout_sec * 1000.0f);

    while (NET_GetAddressStatus(addr) == NET_WAITING) {
        if (SDL_GetTicks() >= deadline_ms) {
            NET_UnrefAddress(addr);
            return -1.0f;
        }
        SDL_Delay(10);
    }
    if (NET_GetAddressStatus(addr) != NET_SUCCESS) {
        NET_UnrefAddress(addr);
        return -1.0f;
    }

    Uint64 t0 = SDL_GetTicks();
    NET_StreamSocket* sock = NET_CreateClient(addr, (Uint16)tcp_port);
    if (sock == NULL) {
        NET_UnrefAddress(addr);
        return -1.0f;
    }

    while (NET_GetConnectionStatus(sock) == NET_WAITING) {
        if (SDL_GetTicks() >= deadline_ms) {
            NET_DestroyStreamSocket(sock);
            NET_UnrefAddress(addr);
            return -1.0f;
        }
        SDL_Delay(5);
    }

    bool ok = (NET_GetConnectionStatus(sock) == NET_SUCCESS);
    Uint64 rtt_ms = SDL_GetTicks() - t0;

    NET_DestroyStreamSocket(sock);
    NET_UnrefAddress(addr);

    return ok ? (float)rtt_ms : -1.0f;
}

void Fistbump_HandleSESSION(const char* line) {
    SDL_sscanf(line, "SESSION %7s", id_buf);
    SDL_Log("Fistbump: received ID: %s\n", id_buf);

    state = FISTBUMP_SENDING_TOKEN;
}

void Fistbump_HandleDAG(const char* line) {
    SDL_sscanf(line, "DAG %8s %127s", dag.code, dag.activate_url);
    SDL_Log("Fistbump: DAG %s, login at %s\n", dag.code, dag.activate_url);

    state = FISTBUMP_AWAITING_LOGIN;
}

static bool ip_is_192_168(const char* s) {
    return strncmp(s, "192.168.", 8) == 0;
}
static bool ip_is_10(const char* s) {
    return strncmp(s, "10.", 3) == 0;
}

// Build a comma-separated list of LAN IPv4 candidates for the UDP_LAN
// advertise. Server picks the pair sharing a /24 prefix. 192.168 ranks
// before 10; 127/169.254/172.16-31 are ignored.
static bool collect_lan_candidates(char* out, size_t out_size) {
    if (out_size == 0) return false;
    out[0] = '\0';
    bool any = false;

    char native_ip[64] = { 0 };
    if (LocalIP_GetPrimary(native_ip, sizeof(native_ip))) {
        if (ip_is_192_168(native_ip) || ip_is_10(native_ip)) {
            SDL_strlcpy(out, native_ip, out_size);
            any = true;
        }
        Netplay_Log("CONFIG", "native lan ip=%s", native_ip);
    }

    int n = 0;
    NET_Address** addrs = NET_GetLocalAddresses(&n);
    if (addrs == NULL || n <= 0) {
        if (addrs) NET_FreeLocalAddresses(addrs);
        return any;
    }

    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < n; i++) {
            const char* s = NET_GetAddressString(addrs[i]);
            if (s == NULL) continue;
            bool match = (pass == 0) ? ip_is_192_168(s) : ip_is_10(s);
            if (!match) continue;
            if (SDL_strstr(out, s) != NULL) continue;
            size_t cur_len = SDL_strlen(out);
            size_t add_len = SDL_strlen(s) + (cur_len > 0 ? 1 : 0);
            if (cur_len + add_len + 1 > out_size) break;
            if (cur_len > 0) {
                SDL_strlcat(out, ",", out_size);
            }
            SDL_strlcat(out, s, out_size);
            any = true;
        }
    }

    for (int i = 0; i < n; i++) {
        const char* s = NET_GetAddressString(addrs[i]);
        if (s) Netplay_Log("CONFIG", "interface candidate=%s", s);
    }

    NET_FreeLocalAddresses(addrs);
    return any;
}

void Fistbump_HandleUDP(const char* line) {
    char res[8];

    SDL_sscanf(line, "UDP %7s", res);

    if (strcmp(res, "ok") == 0) {
        SDL_Log("Fistbump: UDP ok!\n");
    }
}

// UDP_LAN advertise — called from HandlePROFILE so the server has lan_ips
// cached well ahead of any START dispatch.
static void advertise_lan_ip(void) {
    if (tcp_sock == NULL) {
        return;
    }
    char lan_list[256];
    if (!collect_lan_candidates(lan_list, sizeof(lan_list))) {
        SDL_Log("Fistbump: no usable LAN IPv4 — LAN-direct disabled for this session");
        Netplay_Log("CONFIG", "lan candidates=none");
        return;
    }
    char buf[320];
    int len = SDL_snprintf(buf, sizeof(buf), "UDP_LAN %s\n", lan_list);
    if (len > 0 && len < (int)sizeof(buf)) {
        NET_WriteToStreamSocket(tcp_sock, buf, len);
        SDL_Log("Fistbump: advertised LAN ips=%s", lan_list);
        Netplay_Log("CONFIG", "advertised lan_ips=%s", lan_list);
    }
}

void Fistbump_HandleTOKEN(const char* line) {
    char token[1024];
    int expiry;

    if (sscanf(line, "TOKEN refresh %1023s %d", token, &expiry) == 2) {
        SDL_strlcpy(refresh_token.token, token, sizeof(refresh_token.token));
        refresh_token.expiry = expiry;
        SaveToken(&refresh_token);
        state = FISTBUMP_IDLE;
    }
}

void Fistbump_HandlePROFILE(const char* line) {
    SDL_sscanf(line, "PROFILE %7s", profile.username);
    SDL_Log("Fistbump: Logged in as %s\n", profile.username);
    Netplay_Log("AUTH", "logged in user=%s", profile.username);

    advertise_lan_ip();

    const Args* a = get_args();
    if (a != NULL && a->netplay.force_relay) {
        const char* cmd = "SET force_relay 1\n";
        NET_WriteToStreamSocket(tcp_sock, cmd, (int)SDL_strlen(cmd));
        SDL_Log("Fistbump: force_relay=1 advertised to server");
    }
}

void Fistbump_HandleMATCH(const char* line) {
    SDL_sscanf(line, "MATCH %36s %63s", match_result.match_id, match_result.opponent_name);
    SDL_Log("Fistbump: matched with %s\n", match_result.opponent_name);
    Netplay_Log("QUEUE", "matched id=%s opp=%s", match_result.match_id, match_result.opponent_name);

    result_sent = false;
    state = FISTBUMP_MATCHED;
}

void Fistbump_HandleCHAT(const char* line) {
    // Format: "CHAT <scope> <author> <text...>"
    char scope[16];
    char author[32];
    int prefix_consumed = 0;
    if (SDL_sscanf(line, "CHAT %15s %31s %n", scope, author, &prefix_consumed) < 2) {
        SDL_Log("Fistbump: failed to parse CHAT line: %s", line);
        return;
    }
    const char* text = line + prefix_consumed;
    if (text == NULL || *text == '\0') return;
    Chat_HandleIncomingLine(scope, author, text);
}

bool Fistbump_SendChatRaw(const char* buf, size_t len) {
    if (tcp_sock == NULL || buf == NULL || len == 0) {
        return false;
    }
    NET_WriteToStreamSocket(tcp_sock, buf, (int)len);
    return true;
}

void Fistbump_HandleCANCEL(const char* line) {
    char match_id[37];

    if (SDL_sscanf(line, "CANCEL %36s", match_id) != 1) {
        SDL_Log("Fistbump: failed to parse CANCEL\n");
        return;
    }

    if (strcmp(match_id, match_result.match_id) != 0) {
        return;
    }

    SDL_Log("Fistbump: match cancelled\n");
    Netplay_Log("CANCEL", "peer disconnected mid-match state=%d", (int)state);
    SDL_zero(match_result);

    if (state == FISTBUMP_MATCHED) {
        state = FISTBUMP_IDLE;
    } else if (state == FISTBUMP_GAME_START) {
        // Fast-disconnect: peer closed TCP → tear down without waiting gekko UDP timeout.
        extern bool fistbump_peer_cancelled;
        fistbump_peer_cancelled = true;
    }
}

void Fistbump_HandleSTART(const char* line) {
    // Newest format (Phase-2 mode-select):
    //   "START <player> <relay_ip>:<relay_port> <uuid> <use_relay> <direct_ip>:<direct_port>"
    // Previous extended:
    //   "START <player> <ip>:<port> <uuid> <use_relay>"
    // Legacy direct:
    //   "START <player> <ip>:<port>"
    int player = 0;
    char ip[64] = { 0 };
    int port = 0;
    char muuid[37] = { 0 };
    int use_relay = 0;
    char direct_ip[64] = { 0 };
    int direct_port = 0;

    int n = SDL_sscanf(line, "START %d %63[^:]:%d %36s %d %63[^:]:%d",
                       &player, ip, &port, muuid, &use_relay,
                       direct_ip, &direct_port);
    if (n < 3) {
        SDL_Log("Fistbump: malformed START: %s", line);
        return;
    }

    match_result.player = player;

    if (n >= 5) {
        // Relay endpoint comes first; direct endpoint (if any) is at the tail.
        SDL_strlcpy(match_result.relay_ip, ip, sizeof(match_result.relay_ip));
        match_result.relay_port = port;
        SDL_strlcpy(match_result.match_id, muuid, sizeof(match_result.match_id));
        match_result.use_relay = (use_relay != 0);
        if (n >= 7) {
            SDL_strlcpy(match_result.ip, direct_ip, sizeof(match_result.ip));
            match_result.remote_port = direct_port;
        } else {
            match_result.ip[0] = '\0';
            match_result.remote_port = 0;
        }
    } else {
        // Legacy direct-only format.
        SDL_strlcpy(match_result.ip, ip, sizeof(match_result.ip));
        match_result.remote_port = port;
        match_result.use_relay = false;
        match_result.relay_ip[0] = '\0';
        match_result.relay_port = 0;
    }

    Netplay_Log("MATCH", "start id=%s player=%d use_relay=%d peer=%s:%d",
                match_result.match_id, match_result.player,
                match_result.use_relay ? 1 : 0,
                match_result.ip[0] ? match_result.ip : "(none)",
                match_result.remote_port);
    state = FISTBUMP_GAME_START;
}

static void Fistbump_HandleREJECT(const char* line) {
    const char* reason = line + 7; // skip "REJECT "
    while (*reason == ' ') reason++;
    SDL_strlcpy(last_error, reason, sizeof(last_error));
    SDL_Log("Fistbump: REJECT: %s", reason);
    Netplay_Log("AUTH", "reject reason=\"%s\" state=%d", reason, (int)state);

    if (state != FISTBUMP_LOGGING_IN && state != FISTBUMP_AWAITING_LOGIN) {
        return;
    }

    // Only nuke the token when the server explicitly rejects it; keep it across other REJECT reasons.
    const bool token_bad = (SDL_strstr(reason, "token") != NULL) ||
                           (SDL_strstr(reason, "user no longer exists") != NULL);
    if (token_bad && refresh_token.token[0] != '\0') {
        DeleteToken();
        SDL_memset(&refresh_token, 0, sizeof(refresh_token));
    }
    state = FISTBUMP_AWAITING_CREDENTIALS;
}

void Fistbump_ParseCommand(const char* line) {
    if (strncmp(line, "SESSION ", 8) == 0) {
        Fistbump_HandleSESSION(line);
    } else if (strncmp(line, "DAG ", 4) == 0) {
        Fistbump_HandleDAG(line);
    } else if (strncmp(line, "UDP ", 4) == 0) {
        Fistbump_HandleUDP(line);
    } else if (strncmp(line, "TOKEN ", 6) == 0) {
        Fistbump_HandleTOKEN(line);
    } else if (strncmp(line, "PROFILE ", 8) == 0) {
        Fistbump_HandlePROFILE(line);
    } else if (strncmp(line, "MATCH ", 6) == 0) {
        Fistbump_HandleMATCH(line);
    } else if (strncmp(line, "CANCEL ", 7) == 0) {
        Fistbump_HandleCANCEL(line);
    } else if (strncmp(line, "START ", 6) == 0) {
        Fistbump_HandleSTART(line);
    } else if (strncmp(line, "CHAT ", 5) == 0) {
        Fistbump_HandleCHAT(line);
    } else if (strncmp(line, "ROOM ", 5) == 0) {
        Fistbump_HandleROOM(line);
    } else if (strncmp(line, "REJECT ", 7) == 0) {
        Fistbump_HandleREJECT(line);
    } else if (strncmp(line, "LOADING ", 8) == 0) {
        extern bool fistbump_peer_loading;
        fistbump_peer_loading = (line[8] == '1');
        Netplay_Log("LOADING", "peer=%d", fistbump_peer_loading ? 1 : 0);
    }
}

// Peer status flags read by the netplay layer to gate gekko advance and
// fast-disconnect respectively.
bool fistbump_peer_loading = false;
bool fistbump_peer_cancelled = false;

// Pump hook fired between AFS_ReadSync chunks (see port/io/afs.c) — keeps
// the TCP channel alive + advertises LOADING during slow flash reads.
void AFS_OnSyncTick(void) {
    if (tcp_sock == NULL || state != FISTBUMP_GAME_START) {
        return;
    }
    Fistbump_TickLoadingSignal(true);
    Fistbump_Run();
}

// Edge-detected: emits "LOADING 1" / "LOADING 0" only on transitions.
void Fistbump_TickLoadingSignal(bool local_busy) {
    static bool last_sent = false;
    if (tcp_sock == NULL || state != FISTBUMP_GAME_START) {
        last_sent = false;
        return;
    }
    if (local_busy == last_sent) {
        return;
    }
    char msg[16];
    SDL_snprintf(msg, sizeof(msg), "LOADING %d\n", local_busy ? 1 : 0);
    NET_WriteToStreamSocket(tcp_sock, msg, (int)SDL_strlen(msg));
    Netplay_Log("LOADING", "local=%d", local_busy ? 1 : 0);
    last_sent = local_busy;
}

void Fistbump_HandleROOM(const char* line) {
    // "ROOM CREATED <code> <name>"
    // "ROOM JOINED  <code> <name>"
    // "ROOM LEFT    <code>"
    // "ROOM STATE   <code> host=X best_of=N timer=N damage=N slot_a=X slot_b=X match=X|- members=u1,u2,..."
    // "ROOM INFO/LIST_END" -- ignore
    char sub[16] = { 0 };
    char code[16] = { 0 };
    char rest[512] = { 0 };
    int n = SDL_sscanf(line, "ROOM %15s %15s %511[^\n]", sub, code, rest);

    if (n < 1) return;

    if (SDL_strcmp(sub, "CREATED") == 0 || SDL_strcmp(sub, "JOINED") == 0) {
        SDL_zero(current_room);
        SDL_strlcpy(current_room.code, code, sizeof(current_room.code));
        SDL_strlcpy(current_room.name, rest, sizeof(current_room.name));
        in_room = true;
        Netplay_Log("ROOM", "%s code=%s", sub, code);
    } else if (SDL_strcmp(sub, "LEFT") == 0) {
        SDL_zero(current_room);
        in_room = false;
        Netplay_Log("ROOM", "LEFT code=%s", code);
    } else if (SDL_strcmp(sub, "STATE") == 0 && in_room) {
        // Parse k=v tokens from rest. Format:
        //   host=<user> best_of=3 timer=99 damage=1 slot_a=<user> slot_b=- match=- members=<user>,<user>
        char host[16] = "-", sa[16] = "-", sb[16] = "-", match_id[40] = "-", members[256] = "-";
        int bo = 3, tm = 99, dmg = 1;
        // Walk space-separated tokens, parse key=value.
        const char* p = rest;
        while (*p) {
            while (*p == ' ') p++;
            if (!*p) break;
            const char* key_start = p;
            while (*p && *p != '=' && *p != ' ') p++;
            if (*p != '=') break;
            int key_len = (int)(p - key_start);
            p++;
            const char* val_start = p;
            while (*p && *p != ' ') p++;
            int val_len = (int)(p - val_start);
            if (key_len <= 0 || val_len <= 0) continue;
            #define MATCH_KEY(s) (key_len == (int)(sizeof(s)-1) && SDL_strncmp(key_start, s, sizeof(s)-1) == 0)
            char tmpv[256];
            int copy = val_len < (int)sizeof(tmpv) - 1 ? val_len : (int)sizeof(tmpv) - 1;
            SDL_memcpy(tmpv, val_start, copy);
            tmpv[copy] = '\0';
            if (MATCH_KEY("host"))         SDL_strlcpy(host, tmpv, sizeof(host));
            else if (MATCH_KEY("best_of")) bo = SDL_atoi(tmpv);
            else if (MATCH_KEY("timer"))   tm = SDL_atoi(tmpv);
            else if (MATCH_KEY("damage"))  dmg = SDL_atoi(tmpv);
            else if (MATCH_KEY("slot_a"))  SDL_strlcpy(sa, tmpv, sizeof(sa));
            else if (MATCH_KEY("slot_b"))  SDL_strlcpy(sb, tmpv, sizeof(sb));
            else if (MATCH_KEY("match"))   SDL_strlcpy(match_id, tmpv, sizeof(match_id));
            else if (MATCH_KEY("members")) SDL_strlcpy(members, tmpv, sizeof(members));
            #undef MATCH_KEY
        }
        SDL_strlcpy(current_room.host_name, host, sizeof(current_room.host_name));
        SDL_strlcpy(current_room.slot_a_name, sa, sizeof(current_room.slot_a_name));
        SDL_strlcpy(current_room.slot_b_name, sb, sizeof(current_room.slot_b_name));
        current_room.settings_best_of = bo;
        current_room.settings_timer = tm;
        current_room.settings_damage = dmg;
        current_room.match_in_progress = (match_id[0] != '-' && match_id[0] != '\0');
        // Split members list by comma.
        current_room.members = 0;
        const char* mp = members;
        while (*mp && current_room.members < FISTBUMP_ROOM_MAX_MEMBERS) {
            int oi = 0;
            while (*mp && *mp != ',' && oi + 1 < (int)sizeof(current_room.member_names[0])) {
                current_room.member_names[current_room.members][oi++] = *mp++;
            }
            current_room.member_names[current_room.members][oi] = '\0';
            current_room.members += 1;
            while (*mp && *mp != ',') mp++;
            if (*mp == ',') mp++;
        }
        current_room.peer_present = current_room.members >= 2;
        // Local-user flags vs profile.username.
        const char* me = profile.username;
        current_room.is_host   = (me[0] && SDL_strcmp(host, me) == 0);
        current_room.is_slot_a = (me[0] && SDL_strcmp(sa, me) == 0);
        current_room.is_slot_b = (me[0] && SDL_strcmp(sb, me) == 0);
        Netplay_Log("ROOM", "STATE code=%s members=%d host=%s slots=%s/%s match=%s",
                    code, current_room.members, host, sa, sb, match_id);
    }
}

const Fistbump_Room* Fistbump_GetRoom(void) {
    return in_room ? &current_room : NULL;
}

void Fistbump_Run() {
    char tmp[1024];

    read_into_line_buf();

    while (pop_line(tmp, sizeof(tmp))) {
        Fistbump_ParseCommand(tmp);
    }

    switch (state) {
    case FISTBUMP_IDLE:
    case FISTBUMP_CONNECTING:
        Fistbump_Connect();
        break;

    case FISTBUMP_SENDING_TOKEN:
        Fistbump_Login();
        break;

    case FISTBUMP_LOGGING_IN:
    case FISTBUMP_AWAITING_LOGIN:
    case FISTBUMP_AWAITING_CREDENTIALS:
    case FISTBUMP_AWAITING_MATCH:
    case FISTBUMP_MATCHED:
        break;

    case FISTBUMP_SENDING_UDP:
        Fistbump_SendUDP();
        break;

    case FISTBUMP_GAME_START:
    case FISTBUMP_ERROR:
        break;
    }
}

FistbumpState Fistbump_GetState() {
    return state;
}

FistbumpConnectState Fistbump_GetConnectState() {
    return connect_state;
}

const MatchResult* Fistbump_GetResult() {
    return &match_result;
}

NET_DatagramSocket* Fistbump_GetSocket() {
    return udp_sock;
}

DAG Fistbump_GetDAG() {
    return dag;
}

bool Fistbump_IsLoggedIn() {
    return strcmp(profile.username, "") != 0;
}

void Fistbump_Logout() {
    DeleteToken();
    Fistbump_Reset();
}

void Fistbump_Reset() {
    if (tcp_sock != NULL) {
        NET_DestroyStreamSocket(tcp_sock);
        tcp_sock = NULL;
    }

    if (udp_sock != NULL) {
        NET_DestroyDatagramSocket(udp_sock);
        udp_sock = NULL;
    }

    if (server_addr != NULL) {
        NET_UnrefAddress(server_addr);
        server_addr = NULL;
    }

    SDL_zeroa(id_buf);
    SDL_zeroa(line_buf);
    line_len = 0;
    udp_retry_timer = 0;
    SDL_zero(match_result);
    SDL_zero(current_room);
    in_room = false;

    state = FISTBUMP_IDLE;
    memset(&profile, 0, sizeof(profile));
    NET_Quit();
}

void Fistbump_EndMatch() {
    // Drop the per-match UDP socket; keep TCP + profile + room so the user stays in the room.
    if (udp_sock != NULL) {
        NET_DestroyDatagramSocket(udp_sock);
        udp_sock = NULL;
    }
    SDL_zero(match_result);
    udp_retry_timer = 0;
    state = FISTBUMP_IDLE;
}

#endif
