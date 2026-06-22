#if NETPLAY_ENABLED

#include "port/sdl/netstats_renderer.h"
#include "platform/netplay/netplay.h"
#include "sf33rd/Source/Game/ui/sc_sub.h"

#include <SDL3/SDL.h>

void NetstatsRenderer_Render() {
#if !IMGUI
    // PC/Android already show ping + rollback in the ImGui FPS/netstats overlay,
    // so this SF3-text readout is redundant there — only draw it on builds
    // without the overlay (Vita).
    if (Netplay_GetSessionState() != NETPLAY_SESSION_RUNNING) {
        return;
    }

    NetworkStats stats = { 0 };
    Netplay_GetNetworkStats(&stats);

    char buffer[32];
    SDL_snprintf(buffer, sizeof(buffer), "R:%d P:%d", stats.rollback, stats.ping);

    SSPutStrPro(0, 2, 2, 9, 0xFFFFFFFF, buffer);
#endif
}

#endif // NETPLAY_ENABLED
