#if NETPLAY_ENABLED

#include "platform/netplay/regions.h"
#include "platform/netplay/fistbump.h"
#include "platform/netplay/netplay.h"
#include "platform/netplay/netplay_log.h"
#include "port/config/config.h"
#include "port/paths.h"
#include "port/resources.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <string.h>

static Region regions[REGIONS_MAX];
static int  region_count    = 0;
static int  selected_idx    = 0;
static bool pinging         = false;
static SDL_Thread* ping_th  = NULL;

static char* trim(char* s) {
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    char* end = s + SDL_strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) end--;
    *end = '\0';
    return s;
}

static bool parse_line(char* line) {
    if (region_count >= REGIONS_MAX) return false;
    line = trim(line);
    if (line[0] == '\0' || line[0] == '#') return false;

    char* fields[4] = { 0 };
    int n = 0;
    char* cur = line;
    while (n < 4 && cur != NULL) {
        fields[n++] = cur;
        char* sep = SDL_strchr(cur, '|');
        if (sep == NULL) break;
        *sep = '\0';
        cur = sep + 1;
    }
    if (n != 4) return false;

    Region* r = &regions[region_count];
    SDL_zerop(r);
    SDL_strlcpy(r->code,  trim(fields[0]), sizeof(r->code));
    SDL_strlcpy(r->label, trim(fields[1]), sizeof(r->label));
    SDL_strlcpy(r->ip,    trim(fields[2]), sizeof(r->ip));
    r->port = SDL_atoi(trim(fields[3]));
    r->ping_ms = REGION_PING_UNTESTED;
    if (r->code[0] == '\0' || r->ip[0] == '\0' || r->port <= 0) return false;
    region_count += 1;
    return true;
}

static bool load_from_file(const char* path) {
    SDL_IOStream* io = SDL_IOFromFile(path, "r");
    if (io == NULL) return false;
    Sint64 size = SDL_GetIOSize(io);
    if (size <= 0 || size > 16 * 1024) { SDL_CloseIO(io); return false; }
    char* buf = (char*)SDL_malloc((size_t)size + 1);
    if (buf == NULL) { SDL_CloseIO(io); return false; }
    size_t read = SDL_ReadIO(io, buf, (size_t)size);
    SDL_CloseIO(io);
    buf[read] = '\0';

    const int before = region_count;
    char* p = buf;
    while (*p != '\0' && region_count < REGIONS_MAX) {
        char* nl = SDL_strchr(p, '\n');
        if (nl != NULL) *nl = '\0';
        parse_line(p);
        if (nl == NULL) break;
        p = nl + 1;
    }
    SDL_free(buf);
    return region_count > before;
}

static void Regions_Load(void) {
    if (region_count > 0) return;

    char path[512];
    SDL_snprintf(path, sizeof(path), "%sregions.txt", Paths_GetPrefPath());
    if (load_from_file(path)) {
        Netplay_Log("REGION", "loaded %d region(s) from prefpath=%s", region_count, path);
        return;
    }

    char* res = Resources_GetPath("regions.txt");
    if (res != NULL) {
        const bool ok = load_from_file(res);
        if (ok) {
            Netplay_Log("REGION", "loaded %d region(s) from resources=%s", region_count, res);
        }
        SDL_free(res);
        if (region_count > 0) return;
    }

    // Also probe alongside the executable so a flat zip distribution works
    // without copying anything into the user's prefpath first.
    const char* base = Paths_GetBasePath();
    if (base != NULL) {
        SDL_snprintf(path, sizeof(path), "%sregions.txt", base);
        if (load_from_file(path)) {
            Netplay_Log("REGION", "loaded %d region(s) from basepath=%s", region_count, path);
            return;
        }
    }

    Netplay_Log("REGION", "no regions.txt found (probed prefpath=%s, resources=<prefpath>/resources/, basepath=%s) — region picker will show (unknown)",
                Paths_GetPrefPath(), base ? base : "(null)");
}

const Region* Regions_Get(int idx) {
    Regions_Load();
    if (idx < 0 || idx >= region_count) return NULL;
    return &regions[idx];
}

int Regions_Count(void) {
    Regions_Load();
    return region_count;
}

int Regions_GetSelectedIdx(void) {
    return selected_idx;
}

void Regions_LoadPersistedSelection(void) {
    Regions_Load();
    int idx = Config_GetInt(CFG_KEY_NETPLAY_REGION_IDX);
    if (idx >= 0 && idx < region_count) {
        selected_idx = idx;
    }
}

void Regions_Select(int idx) {
    Regions_Load();
    if (idx < 0 || idx >= region_count) return;
    const int prev = selected_idx;
    selected_idx = idx;
    Config_SetInt(CFG_KEY_NETPLAY_REGION_IDX, idx);
    Netplay_SetMatchmakingParams(regions[idx].ip, regions[idx].port);
    Fistbump_Reset();
    Fistbump_Start(regions[idx].ip, regions[idx].port, FISTBUMP_LOCAL_UDP_PORT, Paths_GetPrefPath());
    Netplay_Log("REGION", "switch from=%s to=%s",
                (prev >= 0 && prev < region_count) ? regions[prev].code : "?",
                regions[idx].code);
}

void Regions_SetPing(int idx, float ms) {
    if (idx < 0 || idx >= region_count) return;
    regions[idx].ping_ms = ms;
}

static int ping_worker(void* user_data) {
    (void)user_data;
    for (int i = 0; i < region_count; i++) {
        regions[i].ping_ms = REGION_PING_PENDING;
        regions[i].ping_ms = Fistbump_PingHost(regions[i].ip, regions[i].port, 2.0f);
    }
    pinging = false;
    return 0;
}

void Regions_PingAllAsync(void) {
    if (pinging) return;
    if (ping_th != NULL) {
        SDL_WaitThread(ping_th, NULL);
        ping_th = NULL;
    }
    pinging = true;
    ping_th = SDL_CreateThread(ping_worker, "regions_ping", NULL);
}

bool Regions_IsPinging(void) {
    return pinging;
}

void Regions_FormatLabel(const Region* r, char* out, int out_size) {
    if (r == NULL || out == NULL || out_size <= 0) {
        return;
    }
    if (r->ping_ms >= 0.0f) {
        SDL_snprintf(out, out_size, "%s -- %.0f ms", r->label, (double)r->ping_ms);
    } else if (r->ping_ms < -1.5f) { // PENDING (-2.0) vs UNTESTED (-1.0)
        SDL_snprintf(out, out_size, "%s -- pinging...", r->label);
    } else {
        SDL_snprintf(out, out_size, "%s", r->label);
    }
}

#endif
