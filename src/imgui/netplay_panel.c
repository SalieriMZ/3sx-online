#if IMGUI

#include "imgui/netplay_panel.h"
#include "imgui/dcimgui/dcimgui.h"
#include <SDL3/SDL.h>
#if NETPLAY_ENABLED
#include "platform/netplay/fistbump.h"
#include "platform/netplay/netplay.h"
#endif

#if NETPLAY_ENABLED
static char room_code_buf[16] = { 0 };
static char room_name_buf[32] = { 0 };
static bool visible = false;
#endif

void NetplayPanel_Init(void) {
#if NETPLAY_ENABLED
    room_code_buf[0] = '\0';
    room_name_buf[0] = '\0';
    visible = false;
#endif
}

void NetplayPanel_Show(void) {
#if NETPLAY_ENABLED
    visible = true;
#endif
}

void NetplayPanel_Hide(void) {
#if NETPLAY_ENABLED
    visible = false;
#endif
}

bool NetplayPanel_IsVisible(void) {
#if NETPLAY_ENABLED
    return visible;
#else
    return false;
#endif
}

void NetplayPanel_Render(void) {
#if NETPLAY_ENABLED
    if (!visible) {
        return;  // off-Network-menu: queue / room buttons would corrupt the
                 // SF3 task graph when MATCHED fires outside the netplay menu.
    }
    const FistbumpState s = Fistbump_GetState();
    if (s == FISTBUMP_IDLE && !Fistbump_IsLoggedIn()) {
        return;  // hidden when not logged in
    }
    if (s == FISTBUMP_GAME_START) {
        return;  // gameplay running; hide
    }

    ImGui_SetNextWindowSize((ImVec2){ 380, 280 }, ImGuiCond_FirstUseEver);
    if (ImGui_Begin("Netplay", NULL, 0)) {
        switch (s) {
            case FISTBUMP_IDLE: {
                const Fistbump_Room* rm = Fistbump_GetRoom();
                if (rm != NULL) {
                    ImGui_TextColored((ImVec4){0.3f, 1.0f, 0.4f, 1.0f},
                                       "Room %s  (host: %s)",
                                       rm->code,
                                       rm->host_name[0] ? rm->host_name : "?");
                    if (rm->name[0]) {
                        ImGui_Text("Name: %s", rm->name);
                    }
                    ImGui_Text("Members: %d/%d", rm->members, FISTBUMP_ROOM_MAX_MEMBERS);

                    // Multi-fight scoreboard: cumulative wins for the lifetime
                    // of the room. Slotted (about-to-fight) members in gold.
                    ImGui_Separator();
                    ImGui_TextDisabled("Scoreboard");
                    for (int mi = 0; mi < rm->members; mi++) {
                        const char* mn = rm->member_names[mi];
                        const bool in_slot = SDL_strcmp(mn, rm->slot_a_name) == 0
                                          || SDL_strcmp(mn, rm->slot_b_name) == 0;
                        if (in_slot) {
                            ImGui_TextColored((ImVec4){1.0f, 0.85f, 0.3f, 1.0f},
                                               "%s — %d win%s", mn, rm->member_wins[mi],
                                               rm->member_wins[mi] == 1 ? "" : "s");
                        } else {
                            ImGui_Text("%s — %d win%s", mn, rm->member_wins[mi],
                                       rm->member_wins[mi] == 1 ? "" : "s");
                        }
                    }

                    ImGui_Separator();
                    ImGui_TextDisabled("Match slots");
                    // Slot A
                    {
                        char lbl[48];
                        SDL_snprintf(lbl, sizeof(lbl), "Slot A: %s",
                                     (rm->slot_a_name[0] && rm->slot_a_name[0] != '-')
                                         ? rm->slot_a_name : "(empty)");
                        ImGui_Text("%s", lbl);
                        ImGui_SameLineEx(0, 12);
                        if (rm->is_slot_a) {
                            if (ImGui_Button("Leave##sa")) Fistbump_RoomUnslot();
                        } else if (rm->slot_a_name[0] == '-' || rm->slot_a_name[0] == '\0') {
                            if (ImGui_Button("Take##sa")) Fistbump_RoomSlot('A');
                        } else {
                            ImGui_BeginDisabled(true);
                            ImGui_Button("Taken##sa");
                            ImGui_EndDisabled();
                        }
                    }
                    // Slot B
                    {
                        char lbl[48];
                        SDL_snprintf(lbl, sizeof(lbl), "Slot B: %s",
                                     (rm->slot_b_name[0] && rm->slot_b_name[0] != '-')
                                         ? rm->slot_b_name : "(empty)");
                        ImGui_Text("%s", lbl);
                        ImGui_SameLineEx(0, 12);
                        if (rm->is_slot_b) {
                            if (ImGui_Button("Leave##sb")) Fistbump_RoomUnslot();
                        } else if (rm->slot_b_name[0] == '-' || rm->slot_b_name[0] == '\0') {
                            if (ImGui_Button("Take##sb")) Fistbump_RoomSlot('B');
                        } else {
                            ImGui_BeginDisabled(true);
                            ImGui_Button("Taken##sb");
                            ImGui_EndDisabled();
                        }
                    }

                    // Host-only: Start button. Best-of slider hidden until
                    // we wire it to save_w[Present_Mode].Battle_Number — for
                    // now SF3 uses its built-in BO3. Timer is forced 99
                    // server-side (canonical). Damage hidden entirely — it
                    // was a cheat vector + has no legit use in netplay.
                    if (rm->is_host) {
                        ImGui_Separator();
                        ImGui_TextDisabled("Host controls");
                        const bool can_start = !rm->match_in_progress
                                              && rm->slot_a_name[0] && rm->slot_a_name[0] != '-'
                                              && rm->slot_b_name[0] && rm->slot_b_name[0] != '-';
                        if (!can_start) ImGui_BeginDisabled(true);
                        if (ImGui_Button(rm->match_in_progress ? "Match in progress..." : "Start Match")) {
                            Fistbump_RoomStart();
                        }
                        if (!can_start) ImGui_EndDisabled();
                    } else if (rm->match_in_progress) {
                        ImGui_TextDisabled("Match in progress (host running).");
                    } else {
                        ImGui_TextDisabled("Waiting for host to start match.");
                    }

                    ImGui_Separator();
                    if (ImGui_Button("Leave Room")) {
                        Fistbump_RoomLeave();
                    }
                    break;
                }
                ImGui_Text("Logged in. Pick a mode:");
                ImGui_Separator();
                if (ImGui_Button("Queue Casual")) {
                    if (Fistbump_IsLoggedIn()) {
                        Fistbump_QueueMode("casual");
                    }
                }
                ImGui_SameLineEx(0, 8);
                if (ImGui_Button("Queue Ranked")) {
                    if (Fistbump_IsLoggedIn()) {
                        Fistbump_QueueMode("ranked");
                    }
                }
                ImGui_Separator();
                ImGui_Text("Private room");
                ImGui_SetNextItemWidth(120);
                ImGui_InputTextWithHint("##room_name", "name (optional)",
                                        room_name_buf, sizeof(room_name_buf), 0);
                ImGui_SameLineEx(0, 8);
                if (ImGui_Button("Create")) {
                    Fistbump_RoomCreate(room_name_buf);
                }
                ImGui_SetNextItemWidth(120);
                ImGui_InputTextWithHint("##room_code", "code",
                                        room_code_buf, sizeof(room_code_buf), 0);
                ImGui_SameLineEx(0, 8);
                if (ImGui_Button("Join")) {
                    Fistbump_RoomJoin(room_code_buf);
                }
                break;
            }
            case FISTBUMP_CONNECTING:
            case FISTBUMP_SENDING_TOKEN:
            case FISTBUMP_LOGGING_IN:
                ImGui_Text("Connecting...");
                break;
            case FISTBUMP_AWAITING_LOGIN: {
                DAG d = Fistbump_GetDAG();
                ImGui_Text("Activation code: %s", d.code);
                ImGui_Text("%s", d.activate_url);
                break;
            }
            case FISTBUMP_AWAITING_MATCH:
                ImGui_Text("Searching for opponent...");
                if (ImGui_Button("Cancel")) {
                    Fistbump_CancelQueue();
                }
                break;
            case FISTBUMP_MATCHED: {
                const MatchResult* r = Fistbump_GetResult();
                ImGui_Text("Matched with: %s", r->opponent_name);
                if (ImGui_Button("Accept")) {
                    Fistbump_AcceptMatch();
                }
                ImGui_SameLineEx(0, 8);
                if (ImGui_Button("Decline")) {
                    Fistbump_DeclineMatch();
                }
                break;
            }
            case FISTBUMP_SENDING_UDP:
                ImGui_Text("Starting match...");
                break;
            case FISTBUMP_ERROR:
                ImGui_TextColored((ImVec4){1.0f, 0.3f, 0.3f, 1.0f}, "Error");
                break;
            default:
                ImGui_Text("State: %d", (int)s);
                break;
        }
    }
    ImGui_End();
#endif
}

#endif
