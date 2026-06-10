#if IMGUI && NETPLAY_ENABLED

#include "imgui/login_panel.h"
#include "imgui/dcimgui/dcimgui.h"
#include "platform/netplay/fistbump.h"
#include "platform/netplay/regions.h"
#include "port/config/config.h"
#include "port/creds.h"
#include "args.h"

#include <SDL3/SDL.h>
#include <stdbool.h>

static char username_buf[32]   = { 0 };
static char password_buf[64]   = { 0 };
static bool visible            = false;
static bool remember_password  = false;
static bool force_relay        = false;
static bool force_relay_init   = false;
static bool auto_region_done   = false;
static bool auto_region_picked = false;

void LoginPanel_Init(void) {
    visible = false;
    force_relay = false;
    force_relay_init = false;
    auto_region_done = false;
    auto_region_picked = false;
    username_buf[0] = '\0';
    password_buf[0] = '\0';

    const char* saved = Config_GetString(CFG_KEY_NETPLAY_USERNAME);
    if (saved != NULL && saved[0] != '\0') {
        SDL_strlcpy(username_buf, saved, sizeof(username_buf));
    }

    remember_password = Config_GetBool(CFG_KEY_NETPLAY_REMEMBER_PW);
    if (remember_password) {
        Creds_LoadPassword(password_buf, sizeof(password_buf));
    }
}

void LoginPanel_Show(void) {
    visible = true;
}

void LoginPanel_Hide(void) {
    visible = false;
}

void LoginPanel_Toggle(void) {
    visible = !visible;
}

bool LoginPanel_IsVisible(void) {
    return visible;
}

// Drive Fistbump_Run only during the login portion of the state machine.
// Once we cross into IDLE-after-login the TCP socket goes quiet — match
// flow takes over via Netplay_TickMatchmaking (gated by matchmaking_pending,
// matching stable 1.6.9 behaviour exactly).
void LoginPanel_Tick(void) {
    // Auto-pick best region once after Network menu opens + ping settles.
    // User can override later via the combo (Regions_Select kills any further
    // auto-pick since auto_region_picked stays true until next LoginPanel_Init).
    if (auto_region_done && !auto_region_picked && !Regions_IsPinging()) {
        int best_idx = -1;
        float best_ms = 1e9f;
        for (int i = 0; i < Regions_Count(); i++) {
            const Region* r = Regions_Get(i);
            if (r->ping_ms >= 0.0f && r->ping_ms < best_ms) {
                best_ms = r->ping_ms;
                best_idx = i;
            }
        }
        if (best_idx >= 0 && best_idx != Regions_GetSelectedIdx()) {
            Regions_Select(best_idx);
        }
        auto_region_picked = true;
    }

    const FistbumpState fs = Fistbump_GetState();
    // While in a room we MUST keep pumping Fistbump_Run regardless of state:
    // the matchmaking_pending tick (Netplay_TickMatchmaking) is off post-match
    // and login states are over, but the room needs continuous TCP reads to
    // pick up ROOM STATE broadcasts AND continuous writes (Start/Leave/etc.)
    // to flush. Without this Fistbump_Run pump the room overlay freezes on
    // its last snapshot.
    if (Fistbump_GetRoom() != NULL) {
        Fistbump_Run();
        return;
    }
    switch (fs) {
        case FISTBUMP_CONNECTING:
        case FISTBUMP_SENDING_TOKEN:
        case FISTBUMP_LOGGING_IN:
        case FISTBUMP_AWAITING_LOGIN:
        case FISTBUMP_AWAITING_CREDENTIALS:
            Fistbump_Run();
            break;
        default:
            break;
    }
}

bool LoginPanel_IsActive(void) { return false; }
bool LoginPanel_HasPlayClicked(void) { return true; }

void LoginPanel_Render(void) {
    const FistbumpState fs = Fistbump_GetState();

    // Hide during gameplay so the window doesn't sit on top of the match.
    if (fs == FISTBUMP_GAME_START || fs == FISTBUMP_SENDING_UDP) {
        return;
    }

    if (!visible) {
        return;
    }

    if (!force_relay_init) {
        const Args* a = get_args();
        force_relay = (a != NULL) && (a->netplay.force_relay != 0);
        force_relay_init = true;
    }

    // Kick off auto-ping the first time the panel becomes visible so we have
    // ping data to pick the best region from. User can still manually flip.
    if (!auto_region_done) {
        Regions_PingAllAsync();
        auto_region_done = true;
    }

    const char* err = Fistbump_GetLastError();
    const int   selected = Regions_GetSelectedIdx();
    const Region* cur = Regions_Get(selected);

    // First-launch position only — user can drag and ImGui persists the new
    // position to imgui.ini via Paths_GetPrefPath.
    const ImGuiViewport* vp = ImGui_GetMainViewport();
    ImGui_SetNextWindowPosEx(
        (ImVec2){ vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f },
        ImGuiCond_FirstUseEver,
        (ImVec2){ 0.5f, 0.5f });
    ImGui_SetNextWindowSize((ImVec2){ 520, 400 }, ImGuiCond_FirstUseEver);

    if (!ImGui_Begin("3SX Account", &visible, ImGuiWindowFlags_NoCollapse)) {
        ImGui_End();
        return;
    }

    // Region row.
    ImGui_Text("Region:");
    ImGui_SetNextItemWidth(280);
    const char* preview_label = (cur != NULL) ? cur->label : "(unknown)";
    char preview_buf[160];
    if (cur != NULL && cur->ping_ms >= 0.0f) {
        SDL_snprintf(preview_buf, sizeof(preview_buf), "%s -- %.0f ms",
                     cur->label, cur->ping_ms);
        preview_label = preview_buf;
    } else if (cur != NULL && cur->ping_ms < -1.5f) {
        SDL_snprintf(preview_buf, sizeof(preview_buf), "%s -- pinging...", cur->label);
        preview_label = preview_buf;
    }
    if (ImGui_BeginCombo("##region", preview_label, 0)) {
        for (int i = 0; i < Regions_Count(); i++) {
            const Region* r = Regions_Get(i);
            char item_label[160];
            if (r->ping_ms >= 0.0f) {
                SDL_snprintf(item_label, sizeof(item_label), "%s -- %.0f ms",
                             r->label, r->ping_ms);
            } else if (r->ping_ms < -1.5f) {
                SDL_snprintf(item_label, sizeof(item_label), "%s -- pinging...", r->label);
            } else {
                SDL_snprintf(item_label, sizeof(item_label), "%s", r->label);
            }
            const bool sel = (i == selected);
            if (ImGui_SelectableEx(item_label, sel, 0, (ImVec2){ 0, 0 })) {
                Regions_Select(i);
            }
        }
        ImGui_EndCombo();
    }
    ImGui_SameLineEx(0, 8);
    if (Regions_IsPinging()) {
        ImGui_BeginDisabled(true);
        ImGui_Button("Pinging...");
        ImGui_EndDisabled();
    } else if (ImGui_Button("Ping all")) {
        Regions_PingAllAsync();
    }

    if (ImGui_Checkbox("Force relay (use server, disable P2P)", &force_relay)) {
        Args_SetForceRelay(force_relay ? 1 : 0);
        Config_SetBool(CFG_KEY_NETPLAY_FORCE_RELAY, force_relay);
        if (fs != FISTBUMP_CONNECTING) {
            Fistbump_SetForceRelay(force_relay ? 1 : 0);
        }
    }

    ImGui_Separator();

    switch (fs) {
        case FISTBUMP_CONNECTING:
        case FISTBUMP_SENDING_TOKEN:
            ImGui_Text("Connecting to %s...", (cur != NULL) ? cur->code : "?");
            break;

        case FISTBUMP_LOGGING_IN:
        case FISTBUMP_AWAITING_LOGIN:
            ImGui_Text("Authenticating...");
            break;

        case FISTBUMP_AWAITING_CREDENTIALS: {
            ImGui_Text("Online play requires an account. Sign in or create one:");
            ImGui_SetNextItemWidth(320);
            ImGui_InputTextWithHint("Username", "3-16 chars (a-z, 0-9, _)",
                                    username_buf, sizeof(username_buf), 0);
            ImGui_SetNextItemWidth(320);
            ImGui_InputTextWithHint("Password", "min 6 chars",
                                    password_buf, sizeof(password_buf),
                                    ImGuiInputTextFlags_Password);
            if (ImGui_Checkbox("Remember password (DPAPI-encrypted, this PC only)",
                               &remember_password)) {
                Config_SetBool(CFG_KEY_NETPLAY_REMEMBER_PW, remember_password);
                if (!remember_password) {
                    Creds_ClearPassword();
                }
            }
            const bool have_creds = (username_buf[0] != '\0') && (password_buf[0] != '\0');
            if (!have_creds) {
                ImGui_BeginDisabled(true);
            }
            if (ImGui_Button("Login")) {
                if (remember_password) {
                    Creds_SavePassword(password_buf);
                }
                Fistbump_LoginDirect(username_buf, password_buf);
            }
            ImGui_SameLineEx(0, 8);
            if (ImGui_Button("Register")) {
                if (remember_password) {
                    Creds_SavePassword(password_buf);
                }
                Fistbump_Register(username_buf, password_buf);
            }
            if (!have_creds) {
                ImGui_EndDisabled();
            }
            ImGui_SameLineEx(0, 16);
            if (ImGui_Button("Skip (offline only)")) {
                // Drop TCP socket so the panel collapses to its idle state.
                // User can hit Connect later to bring auth back.
                Fistbump_Reset();
            }
            if (err && err[0]) {
                ImGui_TextColored((ImVec4){ 1.0f, 0.4f, 0.4f, 1.0f }, "%s", err);
            }
            break;
        }

        case FISTBUMP_ERROR:
            ImGui_TextColored((ImVec4){ 1.0f, 0.4f, 0.4f, 1.0f },
                              "Connection error: %s",
                              (err && err[0]) ? err : "(unknown)");
            if (ImGui_Button("Retry")) {
                Regions_Select(selected);
            }
            ImGui_SameLineEx(0, 8);
            if (ImGui_Button("Skip (offline only)")) {
                Fistbump_Reset();
            }
            break;

        case FISTBUMP_IDLE:
            if (Fistbump_IsLoggedIn()) {
                ImGui_TextColored((ImVec4){ 0.4f, 1.0f, 0.4f, 1.0f },
                                  "Signed in as %s on %s",
                                  Fistbump_GetUsername(),
                                  (cur != NULL) ? cur->code : "?");
                if (ImGui_Button("Logout")) {
                    Fistbump_Logout();
                }
            } else {
                ImGui_Text("Offline. Pick a region and click Connect to sign in.");
                if (ImGui_Button("Connect")) {
                    Regions_Select(selected);
                }
            }
            break;

        case FISTBUMP_AWAITING_MATCH:
            ImGui_Text("Searching for match...");
            break;

        case FISTBUMP_MATCHED:
            ImGui_Text("Match found. Accept in Network menu.");
            break;

        default:
            ImGui_Text("State: %d", (int)fs);
            break;
    }

    ImGui_End();
}

#else  // !IMGUI || !NETPLAY_ENABLED

#include "imgui/login_panel.h"

void LoginPanel_Init(void)            {}
void LoginPanel_Render(void)          {}
void LoginPanel_Tick(void)            {}
bool LoginPanel_IsActive(void)        { return false; }
bool LoginPanel_HasPlayClicked(void)  { return true; }

#endif
