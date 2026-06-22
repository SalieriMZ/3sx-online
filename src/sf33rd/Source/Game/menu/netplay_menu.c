#include "sf33rd/Source/Game/menu/netplay_menu.h"
#include "main.h"
#include "platform/netplay/fistbump.h"
#include "platform/netplay/netplay.h"
#include "platform/netplay/netplay_log.h"
#include "port/sdl/netplay_screen.h"
#include "port/sdl/online_ui.h"
#include "sf33rd/AcrSDK/common/pad.h"
#include "sf33rd/Source/Game/effect/eff40.h"
#include "sf33rd/Source/Game/effect/eff57.h"
#include "sf33rd/Source/Game/effect/eff66.h"
#include "sf33rd/Source/Game/effect/eff67.h"
#include "sf33rd/Source/Game/effect/effa4.h"
#include "sf33rd/Source/Game/effect/effect.h"
#include "sf33rd/Source/Game/engine/workuser.h"
#include "sf33rd/Source/Game/menu/menu.h"
#include "sf33rd/Source/Game/sound/sound3rd.h"
#include "sf33rd/Source/Game/system/work_sys.h"
#include "sf33rd/Source/Game/ui/sc_sub.h"

#if NETPLAY_ENABLED
const u8 Netplay_Page_Data[2] = { 0, 2 };

static bool is_logged_in = false;

// Returns true while matchmaking is pending, consuming input to cancel.
// Caller should skip normal menu logic when this returns true.
__attribute__((unused)) static bool check_netplay_cancelled() {
    if (!Netplay_IsMatchmakingPending()) {
        return false;
    }

    // I dont know if we want users to be able to cancel mm on their own?
    // s16 sw = (~plsw_01[0] & plsw_00[0]) | (~plsw_01[1] & plsw_00[1]);

    // if (sw & (SWK_SOUTH | SWK_EAST)) {
    //     Netplay_CancelMatchmaking();
    //     SE_selected();
    // }

    return true;
}

// Shared exit-to-title sequence. Runs from both the native-UI "Back" request
// (OnlineUI_ConsumeExitRequest) and the legacy SF3 cursor fallback.
static void netplay_menu_do_exit(struct _TASK* task_ptr) {
    Menu_Suicide[0] = 0;
    Menu_Suicide[1] = 1;
    Menu_Suicide[2] = 1;
    task_ptr->r_no[1] = 1;
    task_ptr->r_no[2] = 0;
    task_ptr->r_no[3] = 0;
    task_ptr->free[0] = 0;
    Order[115] = 4;
    Order_Timer[115] = 4;

    Netplay_HandleMenuExit();
    OnlineUI_Hide();
    Netplay_Log("MENU", "exit network");
    SE_dir_selected();
}

void Setup_Netplay_Menu(struct _TASK* task_ptr) {
    // Surface the native online UI when the user enters the Network menu. If a
    // refresh token auto-logs-in it shows the signed-in hub; otherwise it
    // prompts for credentials. Hidden again on menu exit.
    OnlineUI_Show();
    Netplay_Log("MENU", "enter network logged_in=%d", Fistbump_IsLoggedIn() ? 1 : 0);

    Menu_Page_Buff = Menu_Page;
    effect_work_init();
    Menu_Common_Init();
    Menu_Cursor_Y[0] = 0;
    Order[0x4E] = 5;
    Order_Timer[0x4E] = 1;
    Menu_Max = Netplay_Page_Data[Menu_Page];
    Order_Dir[0x4E] = 1;
    effect_57_init(0x4E, MENU_HEADER_OPTION_MENU, 0, 0x45, 0);
    Order[0x73] = 3;
    Order_Dir[0x73] = 8;
    Order_Timer[0x73] = 1;
    effect_57_init(0x73, MENU_HEADER_NETWORK, 0, 0x3F, 2);

    // The native online UI (online_ui.c) draws the whole Network menu over the
    // SF3 background, so skip ALL the legacy effect sprites that rendered under
    // it: the effect_66 backdrop panel (the dark box), the effect_A4 cursor rows
    // (FIND MATCH / LOGOUT ACCOUNT) and the effect_40 EXIT button.
}

void Netplay_Menu(struct _TASK* task_ptr) {
    const FistbumpState fs = Fistbump_GetState();
    const bool login_state = Fistbump_IsLoggedIn();

    Menu_Cursor_Y[1] = Menu_Cursor_Y[0];

    switch (task_ptr->r_no[2]) {
    case 0:
        Netplay_BeginMatchmaking();
        Netplay_BeginDirectP2P();

        FadeOut(1, 0xFF, 8);
        task_ptr->r_no[2]++;
        task_ptr->r_no[3] = 0;
        task_ptr->timer = 5;
        task_ptr->free[0] = 0;
        task_ptr->free[1] = 0;
        Menu_Suicide[1] = 1;
        Menu_Suicide[2] = 0;
        Page_Max = 1;
        Menu_Page_Buff = Menu_Page;
        break;

    case 1:
        FadeOut(1, 0xFF, 8);
        task_ptr->r_no[2]++;
        Menu_Page = is_logged_in;
        Setup_Netplay_Menu(task_ptr);
        /* fallthrough */

    case 2:
        FadeOut(1, 0xFF, 8);

        if (fs == FISTBUMP_CONNECTING) {
            break;
        }

        if (--task_ptr->timer == 0) {
            display_netplay_text = true;
            task_ptr->r_no[2]++;
            task_ptr->r_no[3] = 1;
            FadeInit();
        }

        break;

    case 3:
        if (FadeIn(1, 25, 8)) {
            task_ptr->r_no[2]++;
            break;
        }

        break;

    case 4:
        // The native online UI owns all netplay input (login / ranked / casual /
        // rooms / accept / decline / cancel / logout) while it's engaged on this
        // menu; the SF3 cursor handling below is a fallback for when it isn't.
        if (OnlineUI_ConsumeExitRequest()) {
            netplay_menu_do_exit(task_ptr);
            break;
        }
        if (OnlineUI_IsCapturingInput()) {
            break;
        }
        // Match handoff: online_ui draws "Starting match..." but captures no
        // input here; keep the legacy cursor inert so a stray confirm press
        // can't inject a QUEUE / logout mid-punch.
        if (fs == FISTBUMP_GAME_START || fs == FISTBUMP_SENDING_UDP) {
            break;
        }

        Pause_ID = 0;
        Dir_Move_Sub(task_ptr, 0);

        if (IO_Result == 0) {
            Pause_ID = 1;
            Dir_Move_Sub(task_ptr, 1);
        }

        if (Menu_Cursor_Y[1] != Menu_Cursor_Y[0]) {
            SE_cursor_move();
        }

        if ((IO_Result == SWK_EAST || (IO_Result == SWK_SOUTH && Menu_Cursor_Y[0] == Menu_Max && Menu_Page != 0)) &&
            (fs == FISTBUMP_IDLE || fs == FISTBUMP_AWAITING_LOGIN)) {
            netplay_menu_do_exit(task_ptr);
            break;
        } else if (IO_Result == SWK_EAST && Fistbump_GetState() == FISTBUMP_AWAITING_MATCH) {
            Fistbump_CancelQueue();
            break;
        } else if (IO_Result == SWK_EAST && Fistbump_GetState() == FISTBUMP_MATCHED) {
            Fistbump_DeclineMatch();
            break;
        } else if (IO_Result == SWK_SOUTH) {
            if (Fistbump_GetState() == FISTBUMP_MATCHED) {
                Fistbump_AcceptMatch();
                break;
            }

            switch (Menu_Cursor_Y[0]) {
            case 0:
                Netplay_FindMatch();
                break;

            case 1:
                Fistbump_Logout();
                netplay_menu_do_exit(task_ptr);
                break;
            }

            break;
        }

        break;
    }

    if (is_logged_in != login_state) {
        is_logged_in = login_state;
        task[TASK_MENU].r_no[2] = 1;
        task[TASK_MENU].timer = 5;
    }
}
#endif
