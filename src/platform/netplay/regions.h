#ifndef REGIONS_H
#define REGIONS_H

#include <stdbool.h>

// ping_ms sentinels (any negative value is not a real measurement).
#define REGION_PING_UNTESTED (-1.0f)  // never pinged
#define REGION_PING_PENDING  (-2.0f)  // ping in flight

typedef struct {
    char code[16];
    char label[48];
    char ip[64];
    int  port;
    float ping_ms; // >= 0 = ms; else a REGION_PING_* sentinel
} Region;

#define REGIONS_MAX 16

// Format a region's combo/preview label ("<label> -- 42 ms" / "-- pinging..." /
// bare). Single source so the login + settings + Vita pickers can't drift (the
// settings panel was missing the pinging state).
void Regions_FormatLabel(const Region* r, char* out, int out_size);

void Regions_LoadPersistedSelection(void);
const Region* Regions_Get(int idx);
int  Regions_Count(void);
int  Regions_GetSelectedIdx(void);
void Regions_Select(int idx);
void Regions_SetPing(int idx, float ms);
void Regions_PingAllAsync(void);
bool Regions_IsPinging(void);

#endif
