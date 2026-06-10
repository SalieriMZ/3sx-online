#pragma once

#include <stdbool.h>
#include <stddef.h>

// Returns the primary LAN IPv4 address as a dotted-quad string. Used by the
// netplay layer on platforms where SDL3_net's interface enumeration is
// stubbed (Vita) or returns the wrong interface (Android cellular vs WiFi).
bool LocalIP_GetPrimary(char* out, size_t out_size);
