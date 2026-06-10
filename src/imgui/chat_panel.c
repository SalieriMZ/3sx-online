#if IMGUI

#include "imgui/chat_panel.h"
#include "imgui/dcimgui/dcimgui.h"
#if NETPLAY_ENABLED
#include "platform/netplay/chat.h"
#include "platform/netplay/fistbump.h"
#endif

static bool visible = false;
static char input_buf[256];
static int current_scope = 0;  // CHAT_GENERAL
static bool focus_pending = false;

void ChatPanel_Init(void) {
    visible = false;
    input_buf[0] = '\0';
    current_scope = 0;
}

void ChatPanel_Toggle(void) {
#if NETPLAY_ENABLED
    if (!Fistbump_IsLoggedIn()) return;
#endif
    visible = !visible;
    if (visible) {
        focus_pending = true;
    }
}

bool ChatPanel_IsVisible(void) {
    return visible;
}

#if NETPLAY_ENABLED
static const char* scope_label(ChatScope s) {
    switch (s) {
        case CHAT_GENERAL: return "General";
        case CHAT_MATCH:   return "Match";
        case CHAT_ROOM:    return "Room";
        default:           return "?";
    }
}
#endif

void ChatPanel_Render(void) {
    if (!visible) return;
#if NETPLAY_ENABLED
    if (!Fistbump_IsLoggedIn()) { visible = false; return; }

    ImGui_SetNextWindowSize((ImVec2){ 480, 320 }, ImGuiCond_FirstUseEver);
    if (ImGui_Begin("Chat (Tab)", &visible, 0)) {
        if (ImGui_BeginTabBar("##chat_tabs", 0)) {
            for (int s = 0; s < CHAT_SCOPE_COUNT; s++) {
                if (ImGui_BeginTabItem(scope_label((ChatScope)s), NULL, 0)) {
                    current_scope = s;
                    int count = 0;
                    const ChatMsg* msgs = Chat_GetHistory((ChatScope)s, &count);
                    if (ImGui_BeginChild("##history", (ImVec2){0, -32},
                                          ImGuiChildFlags_Borders, 0)) {
                        for (int i = 0; i < count; i++) {
                            ImGui_Text("%s: %s", msgs[i].author, msgs[i].text);
                        }
                        if (ImGui_GetScrollY() >= ImGui_GetScrollMaxY() - 8.0f) {
                            ImGui_SetScrollHereY(1.0f);
                        }
                    }
                    ImGui_EndChild();
                    ImGui_EndTabItem();
                }
            }
            ImGui_EndTabBar();
        }
        ImGui_SetNextItemWidth(-1);
        if (focus_pending) {
            ImGui_SetKeyboardFocusHere();
            focus_pending = false;
        }
        if (ImGui_InputText("##chat_input", input_buf, sizeof(input_buf),
                            ImGuiInputTextFlags_EnterReturnsTrue)) {
            if (input_buf[0]) {
                Chat_Send((ChatScope)current_scope, input_buf);
                input_buf[0] = '\0';
            }
            ImGui_SetKeyboardFocusHereEx(-1);
        }
    }
    ImGui_End();
#endif
}

#endif
