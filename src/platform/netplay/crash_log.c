#include "platform/netplay/crash_log.h"
#include "port/paths.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

static FILE* g_console_log = NULL;
static char g_console_path[512] = { 0 };

static void crashlog_sdl_output(void* userdata, int category, SDL_LogPriority priority, const char* message) {
    (void)userdata;
    (void)category;
    if (g_console_log == NULL || message == NULL) {
        return;
    }
    const char* tag;
    switch (priority) {
        case SDL_LOG_PRIORITY_TRACE:    tag = "TRACE"; break;
        case SDL_LOG_PRIORITY_VERBOSE:  tag = "VERB"; break;
        case SDL_LOG_PRIORITY_DEBUG:    tag = "DBG"; break;
        case SDL_LOG_PRIORITY_INFO:     tag = "INFO"; break;
        case SDL_LOG_PRIORITY_WARN:     tag = "WARN"; break;
        case SDL_LOG_PRIORITY_ERROR:    tag = "ERR"; break;
        case SDL_LOG_PRIORITY_CRITICAL: tag = "CRIT"; break;
        default:                        tag = "LOG"; break;
    }
    fprintf(g_console_log, "%llu [%s] %s\n",
            (unsigned long long)SDL_GetTicks(), tag, message);
    fflush(g_console_log);
}

#ifdef _WIN32
static LONG WINAPI crashlog_seh_filter(EXCEPTION_POINTERS* info) {
    if (g_console_log == NULL) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    const DWORD code = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0;
    const void* addr = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionAddress : NULL;
    fprintf(g_console_log,
            "\n========= CRASH =========\n"
            "code=0x%08lX addr=%p ticks=%llu\n",
            (unsigned long)code,
            addr,
            (unsigned long long)SDL_GetTicks());
    fflush(g_console_log);
    // Let the default handler run after we've recorded; this still terminates.
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

void CrashLog_Init(void) {
    const char* pref = Paths_GetPrefPath();
    if (pref == NULL) {
        return;
    }
    SDL_snprintf(g_console_path, sizeof(g_console_path), "%sconsole.log", pref);

    // Truncate on each launch so logs reflect the most recent session.
    g_console_log = fopen(g_console_path, "w");
    if (g_console_log == NULL) {
        return;
    }
    setvbuf(g_console_log, NULL, _IOLBF, 4096);  // line-buffered

    // Mirror stdout + stderr to the same file. PyInstaller GUI launches the
    // exe with closed stdio handles -- writes can fault. freopen rebinds the
    // CRT handles to our file so any printf-style write lands here instead.
    fprintf(g_console_log, "== console log opened ==\n");
    fflush(g_console_log);

    freopen(g_console_path, "a", stdout);
    freopen(g_console_path, "a", stderr);

    // Force line-buffered (or unbuffered) writes so crash-adjacent printf
    // output reaches disk before the process dies. Without this, default
    // CRT block-buffering swallows the last few KB on hard exits.
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    // Route SDL_Log into the same file.
    SDL_SetLogOutputFunction(crashlog_sdl_output, NULL);

#ifdef _WIN32
    SetUnhandledExceptionFilter(crashlog_seh_filter);
#endif

    SDL_Log("CrashLog initialized at %s", g_console_path);
}

void CrashLog_Shutdown(void) {
    if (g_console_log != NULL) {
        fprintf(g_console_log, "== console log closed ==\n");
        fclose(g_console_log);
        g_console_log = NULL;
    }
}
