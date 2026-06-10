#ifndef PLATFORM_NETPLAY_CRASH_LOG_H
#define PLATFORM_NETPLAY_CRASH_LOG_H

// Redirects stdout/stderr to <PrefPath>/console.log, routes SDL_Log into the
// same file, and installs a Windows SEH handler that flushes the file and
// writes "CRASH: <code>" before the process terminates. Call once at the
// earliest possible point in boot (before any other SDL_Log / printf).
void CrashLog_Init(void);
void CrashLog_Shutdown(void);

#endif
