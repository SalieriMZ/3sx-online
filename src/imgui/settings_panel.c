#if IMGUI

#include "imgui/settings_panel.h"
#include "imgui/dcimgui/dcimgui.h"
#include "platform/app/sdl/sdl_app.h"
#include "port/config/config.h"
#include "sf33rd/Source/Game/system/sys_sub.h"

#if NETPLAY_ENABLED
#include "platform/netplay/fistbump.h"
#include "platform/netplay/netplay.h"
#include "platform/netplay/discord_rpc.h"
#endif

static bool visible = false;
static bool show_fps = false;
static int hud_scale_pct = 100;
static bool discord_rpc_enabled = true;

void SettingsPanel_Init(void) {
    show_fps = Config_GetBool(CFG_KEY_SHOW_FPS);
    int s = Config_GetInt(CFG_KEY_HUD_SCALE_PCT);
    hud_scale_pct = (s >= 50 && s <= 200) ? s : 100;
    discord_rpc_enabled = Config_GetBool(CFG_KEY_DISCORD_RPC);

#ifdef __ANDROID__
    // First-launch default: FPS overlay on so the user sees the counter
    // and can turn it off from Settings. The config persists thereafter.
    static const char* INIT_MARKER = "fps-default-init";
    if (!Config_GetBool(INIT_MARKER)) {
        show_fps = true;
        Config_SetBool(CFG_KEY_SHOW_FPS, true);
        Config_SetBool(INIT_MARKER, true);
    }
#endif
}

void SettingsPanel_Show(void) {
    visible = true;
}

void SettingsPanel_Hide(void) {
    visible = false;
}

void SettingsPanel_Toggle(void) {
    visible = !visible;
}

bool SettingsPanel_IsVisible(void) {
    return visible;
}

void SettingsPanel_Render(void) {
    if (!visible) return;

    ImGui_SetNextWindowSize((ImVec2){ 320, 200 }, ImGuiCond_FirstUseEver);
    if (ImGui_Begin("Settings (F3)", &visible, 0)) {
        if (ImGui_Checkbox("Show FPS", &show_fps)) {
            Config_SetBool(CFG_KEY_SHOW_FPS, show_fps);
        }
        if (ImGui_SliderInt("HUD scale %", &hud_scale_pct, 50, 200)) {
            Config_SetInt(CFG_KEY_HUD_SCALE_PCT, hud_scale_pct);
        }
#if NETPLAY_ENABLED
        if (ImGui_Checkbox("Discord Rich Presence", &discord_rpc_enabled)) {
            Config_SetBool(CFG_KEY_DISCORD_RPC, discord_rpc_enabled);
            DiscordRPC_SetEnabled(discord_rpc_enabled);
            if (discord_rpc_enabled) {
                DiscordRPC_SetActivity("In main menu", NULL, "logo", 0);
            }
        }
        // Region / force-relay / account moved to the native online UI
        // (online_ui.c), reached from the in-game Network menu.
#endif
        ImGui_TextDisabled("Press F3 to toggle this panel.");
    }
    ImGui_End();
}

#ifdef __ANDROID__
// Top-right floating toggle button — Android has no F3 key, so this is the
// only way to surface the overlays during a session. Renders on top of
// everything. Tap = master toggle (mirrors F3 behaviour in sdl_app.c).
static void render_android_menu_button(void) {
    const ImGuiViewport* vp = ImGui_GetMainViewport();
    ImGui_SetNextWindowPosEx(
        (ImVec2){ vp->Pos.x + vp->Size.x - 12.0f, vp->Pos.y + 12.0f },
        ImGuiCond_Always,
        (ImVec2){ 1.0f, 0.0f });
    ImGui_SetNextWindowBgAlpha(0.55f);
    int flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize
              | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing
              | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
    if (ImGui_Begin("##android_menu_btn", NULL, flags)) {
        // Toggles the F3 Settings panel (Android has no F3 key). Account / region
        // / matchmaking are reached from the in-game Network menu (online_ui).
        if (ImGui_ButtonEx("MENU", (ImVec2){ 110, 60 })) {
            visible = !visible;
        }
    }
    ImGui_End();
}
#endif

void SettingsPanel_RenderFpsOverlay(void) {
#ifdef __ANDROID__
    // MENU toggle button is the only way to reach Settings on Android,
    // so it ALWAYS renders regardless of show_fps. FPS counter respects
    // the user's checkbox in Settings → can be disabled there.
    render_android_menu_button();
#endif
    if (!show_fps) return;
    const FrameMetrics* m = SDLApp_GetFrameMetrics();
    if (m == NULL) return;
    // Average last N samples for stable readout.
    float fps_sum = 0;
    float ft_sum = 0;
    int N = (int)SDL_arraysize(m->fps);
    for (int i = 0; i < N; i++) {
        fps_sum += m->fps[i];
        ft_sum += m->frame_time[i];
    }
    float fps_avg = (N > 0) ? (fps_sum / (float)N) : 0.0f;
    float ft_avg_ms = (N > 0) ? ((ft_sum / (float)N) / 1000000.0f) : 0.0f;

    ImGui_SetNextWindowPosEx((ImVec2){ 8, 8 }, ImGuiCond_Always, (ImVec2){ 0, 0 });
    ImGui_SetNextWindowBgAlpha(0.35f);
    int flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize
              | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing
              | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove
              | ImGuiWindowFlags_NoInputs;
    if (ImGui_Begin("##fps_overlay", NULL, flags)) {
        ImGui_Text("FPS %.1f  (%.2f ms)", fps_avg, ft_avg_ms);
#if NETPLAY_ENABLED
        if (Fistbump_GetState() == FISTBUMP_GAME_START) {
            NetworkStats ns = { 0 };
            Netplay_GetNetworkStats(&ns);
            float behind = Netplay_GetFramesBehind();
            // Color-code ping: green <80ms, yellow <160ms, red >=160ms.
            ImVec4 ping_col = (ns.ping < 80)
                ? (ImVec4){ 0.4f, 1.0f, 0.4f, 1.0f }
                : (ns.ping < 160)
                ? (ImVec4){ 1.0f, 0.9f, 0.3f, 1.0f }
                : (ImVec4){ 1.0f, 0.3f, 0.3f, 1.0f };
            const MatchResult* mr = Fistbump_GetResult();
            const char* mode = (mr != NULL && mr->use_relay) ? "server" : "p2p";
            ImGui_TextColored(ping_col, "Ping %d ms (%s)", ns.ping, mode);
            ImGui_Text("Delay %d  Rollback %d", ns.delay, ns.rollback);
            ImGui_Text("Behind: %.1f frames", behind);
        }
#endif
    }
    ImGui_End();
}

#endif
