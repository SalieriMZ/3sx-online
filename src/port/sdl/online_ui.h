#pragma once

// Cross-platform native online UI (PC / Android / Vita), drawn with the SF3
// sprite font (SSPutStrPro) and driven by the pad (io_w) + the TextInput
// backend. Supersedes the ImGui login_panel + netplay_panel AND the Vita-only
// vita_login: one module on every platform for region pick, login/register,
// the matchmaking hub (ranked/casual/rooms/region/relay/logout) and the room
// + accept/decline screens. ImGui survives only for the in-match chat + the
// FPS/netstats HUD.
//
// Lifecycle mirrors the old panels: shown/hidden gated to the SF3 Network
// menu. While engaged it OWNS the netplay input, so the SF3 menu defers to it
// via OnlineUI_IsCapturingInput() and runs its exit sequence when
// OnlineUI_ConsumeExitRequest() fires.

#include <stdbool.h>

#if NETPLAY_ENABLED

void OnlineUI_Init(void* sdl_window); // pass SDL_Window* (NULL on Vita is fine)
void OnlineUI_Show(void);
void OnlineUI_Hide(void);
bool OnlineUI_IsVisible(void);

void OnlineUI_Tick(void);   // pump Fistbump + drive the state machine + input
void OnlineUI_Render(void); // draw via SSPutStrPro (call inside end_frame)

// SDL backends: feed SDL_Event* so the active text field can consume text/keys.
// No-op on Vita (sceIme polls itself). Safe to call always.
void OnlineUI_HandleSDLEvent(const void* sdl_event);

// True while showing an interactive/status netplay screen — the SF3 Network
// menu must skip its own cursor/matchmaking input so the two don't double-fire.
bool OnlineUI_IsCapturingInput(void);

// True exactly once after the user picks "Back" from the hub — the SF3 menu
// polls this to run its existing exit-to-title sequence (online_ui has no
// access to the menu task graph).
bool OnlineUI_ConsumeExitRequest(void);

#else

static inline void OnlineUI_Init(void* w) { (void)w; }
static inline void OnlineUI_Show(void) {}
static inline void OnlineUI_Hide(void) {}
static inline bool OnlineUI_IsVisible(void) { return false; }
static inline void OnlineUI_Tick(void) {}
static inline void OnlineUI_Render(void) {}
static inline void OnlineUI_HandleSDLEvent(const void* e) { (void)e; }
static inline bool OnlineUI_IsCapturingInput(void) { return false; }
static inline bool OnlineUI_ConsumeExitRequest(void) { return false; }

#endif // NETPLAY_ENABLED
