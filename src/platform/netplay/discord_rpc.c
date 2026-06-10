#include "platform/netplay/discord_rpc.h"

#if defined(__vita__) || defined(__ANDROID__) || defined(__PSP__) || defined(__psp2__)
// Stub the public surface as no-ops on platforms without a Discord IPC pipe.
#include <stdbool.h>
void DiscordRPC_Init(const char* app_id) { (void)app_id; }
void DiscordRPC_Shutdown(void) {}
void DiscordRPC_SetEnabled(bool enabled) { (void)enabled; }
void DiscordRPC_SetActivity(const char* state, const char* details,
                            const char* asset, long long start_unix) {
    (void)state; (void)details; (void)asset; (void)start_unix;
}
void DiscordRPC_ClearActivity(void) {}
void DiscordRPC_Tick(void) {}
#else

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

#define DRPC_OP_HANDSHAKE 0
#define DRPC_OP_FRAME     1
#define DRPC_OP_CLOSE     2

static char g_app_id[64] = { 0 };
static bool g_enabled = false;
static bool g_connected = false;

#ifdef _WIN32
static HANDLE g_pipe = INVALID_HANDLE_VALUE;
#else
static int g_pipe = -1;
#endif

static int g_pid = 0;

static int discord_pid(void) {
    if (g_pid) return g_pid;
#ifdef _WIN32
    g_pid = (int)GetCurrentProcessId();
#else
    g_pid = (int)getpid();
#endif
    return g_pid;
}

static bool drpc_open_pipe(void) {
#ifdef _WIN32
    for (int slot = 0; slot < 10; slot++) {
        char path[64];
        SDL_snprintf(path, sizeof(path), "\\\\.\\pipe\\discord-ipc-%d", slot);
        HANDLE h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                                OPEN_EXISTING, 0, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            g_pipe = h;
            SDL_Log("DiscordRPC: opened %s", path);
            return true;
        }
    }
    SDL_Log("DiscordRPC: no discord-ipc-* pipe found (Discord client not running?)");
    return false;
#else
    const char* tmp = getenv("XDG_RUNTIME_DIR");
    if (!tmp) tmp = getenv("TMPDIR");
    if (!tmp) tmp = "/tmp";
    for (int slot = 0; slot < 10; slot++) {
        char path[256];
        SDL_snprintf(path, sizeof(path), "%s/discord-ipc-%d", tmp, slot);
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) return false;
        struct sockaddr_un addr = { 0 };
        addr.sun_family = AF_UNIX;
        SDL_strlcpy(addr.sun_path, path, sizeof(addr.sun_path));
        if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
            g_pipe = fd;
            return true;
        }
        close(fd);
    }
    return false;
#endif
}

static void drpc_close_pipe(void) {
#ifdef _WIN32
    if (g_pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(g_pipe);
        g_pipe = INVALID_HANDLE_VALUE;
    }
#else
    if (g_pipe >= 0) {
        close(g_pipe);
        g_pipe = -1;
    }
#endif
    g_connected = false;
}

static bool drpc_write(int op, const char* json) {
#ifdef _WIN32
    if (g_pipe == INVALID_HANDLE_VALUE) return false;
#else
    if (g_pipe < 0) return false;
#endif
    Uint32 op_le = (Uint32)op;
    Uint32 len_le = (Uint32)strlen(json);
    char hdr[8];
    memcpy(hdr, &op_le, 4);
    memcpy(hdr + 4, &len_le, 4);

#ifdef _WIN32
    DWORD wrote = 0;
    if (!WriteFile(g_pipe, hdr, 8, &wrote, NULL) || wrote != 8) {
        drpc_close_pipe();
        return false;
    }
    if (!WriteFile(g_pipe, json, len_le, &wrote, NULL) || wrote != len_le) {
        drpc_close_pipe();
        return false;
    }
    return true;
#else
    if (write(g_pipe, hdr, 8) != 8) { drpc_close_pipe(); return false; }
    if (write(g_pipe, json, len_le) != (ssize_t)len_le) { drpc_close_pipe(); return false; }
    return true;
#endif
}

static void drpc_drain_logged(void);
static void drpc_drain(void) {
    // Discord sends back a READY frame plus subsequent reply frames. We don't
    // care about contents but must drain or the kernel pipe buffer fills and
    // future writes EWOULDBLOCK. Non-blocking read with a tiny timeout.
#ifdef _WIN32
    if (g_pipe == INVALID_HANDLE_VALUE) return;
    DWORD avail = 0;
    for (int i = 0; i < 8; i++) {
        if (!PeekNamedPipe(g_pipe, NULL, 0, NULL, &avail, NULL)) return;
        if (avail == 0) return;
        char buf[1024];
        DWORD got = 0;
        DWORD chunk = avail > sizeof(buf) ? (DWORD)sizeof(buf) : avail;
        if (!ReadFile(g_pipe, buf, chunk, &got, NULL)) return;
        if (got == 0) return;
    }
#else
    if (g_pipe < 0) return;
    char buf[1024];
    int flags = fcntl(g_pipe, F_GETFL, 0);
    fcntl(g_pipe, F_SETFL, flags | O_NONBLOCK);
    for (int i = 0; i < 8; i++) {
        ssize_t got = read(g_pipe, buf, sizeof(buf));
        if (got <= 0) break;
    }
    fcntl(g_pipe, F_SETFL, flags);
#endif
}

static void drpc_drain_logged(void) {
    // Like drpc_drain but logs the bytes Discord sent back so we can see if a
    // SET_ACTIVITY succeeded or got an error frame.
#ifdef _WIN32
    if (g_pipe == INVALID_HANDLE_VALUE) return;
    DWORD avail = 0;
    if (!PeekNamedPipe(g_pipe, NULL, 0, NULL, &avail, NULL)) return;
    if (avail == 0) {
        SDL_Log("DiscordRPC: drain empty");
        return;
    }
    char buf[2048];
    DWORD got = 0;
    DWORD chunk = avail > sizeof(buf) - 1 ? (DWORD)sizeof(buf) - 1 : avail;
    if (ReadFile(g_pipe, buf, chunk, &got, NULL) && got > 0) {
        buf[got] = '\0';
        // Header is 8 bytes (op + len). Body after.
        const char* body = (got > 8) ? (buf + 8) : buf;
        SDL_Log("DiscordRPC: rx %lu bytes: %s", (unsigned long)got, body);
    }
#else
    if (g_pipe < 0) return;
    char buf[2048];
    int flags = fcntl(g_pipe, F_GETFL, 0);
    fcntl(g_pipe, F_SETFL, flags | O_NONBLOCK);
    ssize_t got = read(g_pipe, buf, sizeof(buf) - 1);
    fcntl(g_pipe, F_SETFL, flags);
    if (got > 0) {
        buf[got] = '\0';
        SDL_Log("DiscordRPC: rx %zd bytes: %s", got, got > 8 ? buf + 8 : buf);
    }
#endif
}

static bool drpc_handshake(void) {
    char json[256];
    SDL_snprintf(json, sizeof(json),
                 "{\"v\":1,\"client_id\":\"%s\"}", g_app_id);
    if (!drpc_write(DRPC_OP_HANDSHAKE, json)) return false;
    SDL_Log("DiscordRPC: handshake sent, polling response (up to 3s)");
    // Poll up to 3s — some Discord clients (Vencord, BetterDiscord, older
    // builds) take longer to reply than the original ~50ms heuristic.
    for (int i = 0; i < 30; i++) {
        SDL_Delay(100);
#ifdef _WIN32
        DWORD avail = 0;
        if (PeekNamedPipe(g_pipe, NULL, 0, NULL, &avail, NULL) && avail > 0) {
            drpc_drain_logged();
            return true;
        }
#endif
    }
    SDL_Log("DiscordRPC: no handshake response (pipe owner not a real Discord client?)");
    return true;  // keep pipe open anyway; SetActivity will still try
}

static bool drpc_ensure_connected(void) {
    if (g_connected) return true;
    if (!g_enabled) return false;
    if (g_app_id[0] == '\0') return false;
    if (!drpc_open_pipe()) return false;
    if (!drpc_handshake()) {
        drpc_close_pipe();
        return false;
    }
    g_connected = true;
    SDL_Log("DiscordRPC: connected (app_id=%s)", g_app_id);
    return true;
}

void DiscordRPC_Init(const char* app_id) {
    if (app_id == NULL) return;
    SDL_strlcpy(g_app_id, app_id, sizeof(g_app_id));
    // Don't auto-connect here; caller controls via DiscordRPC_SetEnabled.
}

void DiscordRPC_SetEnabled(bool enabled) {
    if (g_enabled == enabled) return;
    g_enabled = enabled;
    if (!enabled && g_connected) {
        DiscordRPC_ClearActivity();
        drpc_close_pipe();
    }
}

bool DiscordRPC_IsEnabled(void) {
    return g_enabled;
}

void DiscordRPC_Shutdown(void) {
    if (g_connected) {
        DiscordRPC_ClearActivity();
        drpc_write(DRPC_OP_CLOSE, "{}");
    }
    drpc_close_pipe();
    g_enabled = false;
}

static int json_escape(char* out, size_t out_size, const char* in) {
    size_t j = 0;
    for (size_t i = 0; in[i] && j + 2 < out_size; i++) {
        char c = in[i];
        if (c == '"' || c == '\\') {
            if (j + 3 >= out_size) break;
            out[j++] = '\\';
            out[j++] = c;
        } else if ((unsigned char)c < 0x20) {
            // skip control chars
        } else {
            out[j++] = c;
        }
    }
    out[j] = '\0';
    return (int)j;
}

static int g_nonce_counter = 0;

void DiscordRPC_SetActivity(const char* state, const char* details,
                             const char* image_key, long long start_ts) {
    if (!drpc_ensure_connected()) return;
    (void)image_key;  // asset key disabled until "logo" registered in Dev Portal

    char esc_state[256] = "";
    char esc_details[256] = "";
    if (state)    json_escape(esc_state, sizeof(esc_state), state);
    if (details)  json_escape(esc_details, sizeof(esc_details), details);

    g_nonce_counter += 1;

    char json[1024];
    int n = SDL_snprintf(json, sizeof(json),
        "{\"cmd\":\"SET_ACTIVITY\",\"nonce\":\"a%d\",\"args\":{\"pid\":%d,\"activity\":{",
        g_nonce_counter, discord_pid());

    if (state && state[0]) {
        n += SDL_snprintf(json + n, sizeof(json) - n,
                          "\"state\":\"%s\",", esc_state);
    }
    if (details && details[0]) {
        n += SDL_snprintf(json + n, sizeof(json) - n,
                          "\"details\":\"%s\",", esc_details);
    }
    if (start_ts > 0) {
        n += SDL_snprintf(json + n, sizeof(json) - n,
                          "\"timestamps\":{\"start\":%lld},", start_ts);
    }
    // Trim trailing comma if any.
    if (n > 0 && json[n - 1] == ',') {
        json[n - 1] = '\0';
        n -= 1;
    }
    SDL_snprintf(json + n, sizeof(json) - n, "}}}");

    SDL_Log("DiscordRPC: SET_ACTIVITY %s", json);
    bool ok = drpc_write(DRPC_OP_FRAME, json);
    SDL_Log("DiscordRPC: write %s", ok ? "OK" : "FAIL");
    SDL_Delay(20);
    drpc_drain_logged();
}

void DiscordRPC_ClearActivity(void) {
    if (!g_connected) return;
    char json[128];
    SDL_snprintf(json, sizeof(json),
                 "{\"cmd\":\"SET_ACTIVITY\",\"nonce\":\"clear\",\"args\":{\"pid\":%d,\"activity\":{}}}",
                 discord_pid());
    drpc_write(DRPC_OP_FRAME, json);
}

#endif // stubbed-platform else branch
