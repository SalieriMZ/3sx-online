#ifndef REGIONS_H
#define REGIONS_H

#include <stdbool.h>

typedef struct {
    char code[16];
    char label[48];
    char ip[64];
    int  port;
    float ping_ms; // < 0 = not tested
} Region;

#define REGIONS_MAX 16

void Regions_LoadPersistedSelection(void);
const Region* Regions_Get(int idx);
int  Regions_Count(void);
int  Regions_GetSelectedIdx(void);
void Regions_Select(int idx);
void Regions_SetPing(int idx, float ms);
void Regions_PingAllAsync(void);
bool Regions_IsPinging(void);

#endif
