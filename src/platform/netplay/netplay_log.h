#if NETPLAY_ENABLED

#ifndef NETPLAY_LOG_H
#define NETPLAY_LOG_H

#include <SDL3/SDL.h>

// Verbose netplay logger. Writes to <PrefPath>/netplay.log with timestamps.
// Auto-rotates at ~512KB. Safe to call any time once Netplay_LogInit() has run.
//
// Conventions:
//   - One line per event: "<ts_ms> <category> <message>"
//   - Categories: SESSION, GEKKO, INPUT, ROLLBACK, RELAY, FISTBUMP, DISCONNECT
//   - During an active match, callers should also tick Netplay_LogPerFrame()
//     once per simulated frame so we get a periodic heartbeat with sync info.

void Netplay_LogInit(void);
void Netplay_LogShutdown(void);
void Netplay_Log(const char* category, SDL_PRINTF_FORMAT_STRING const char* fmt, ...) SDL_PRINTF_VARARG_FUNC(2);
void Netplay_LogPerFrame(int frame, float frames_behind, int rollbacks_this_frame);

#endif

#endif
