#if NETPLAY_ENABLED

#include "port/sdl/netplay_screen.h"
#include "main.h"
#include "platform/netplay/fistbump.h"
#include "platform/netplay/netplay.h"
#include "sf33rd/Source/Game/system/work_sys.h"
#include "sf33rd/Source/Game/ui/sc_sub.h"

#include <SDL3/SDL.h>

// Set true by the SF3 Network menu; kept so existing callers stay valid. The
// native online UI (online_ui.c) now owns all pre-match status, and the
// "Match start!" transition flash was removed as redundant — so this is a no-op.
bool display_netplay_text = false;

void NetplayScreen_Render() {
}

#endif // NETPLAY_ENABLED
