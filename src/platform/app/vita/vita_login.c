#if defined(__vita__) && NETPLAY_ENABLED

#include "platform/app/vita/vita_login.h"
#include "platform/netplay/fistbump.h"
#include "platform/netplay/netplay.h"
#include "platform/netplay/netplay_log.h"
#include "platform/netplay/regions.h"
#include "port/paths.h"
#include "sf33rd/AcrSDK/common/pad.h"
#include "sf33rd/AcrSDK/ps2/flps2etc.h"
#include "sf33rd/AcrSDK/ps2/foundaps2.h"
#include "sf33rd/Source/Game/io/ioconv.h"
#include "types.h"

#include <SDL3/SDL.h>
#include <psp2/common_dialog.h>
#include <psp2/ime_dialog.h>
#include <psp2/sysmodule.h>

extern void SSPutStrPro(s16 flag, s16 x, s16 y, u8 atr, u32 vtxcol, const char* str);

#define COLOR_WHITE   0xFFFFFFFF
#define COLOR_YELLOW  0xFFFF00FF
#define COLOR_GRAY    0x808080FF
#define COLOR_GREEN   0x00FF00FF

// Vita SDL3 maps physical X to SWK_WEST (Japanese layout). Accept all
// three face confirm buttons; Triangle/Start/Select cancel.
#define CONFIRM_BUTTONS (SWK_SOUTH | SWK_EAST | SWK_WEST)
#define CANCEL_BUTTONS  (SWK_NORTH | SWK_START | SWK_BACK)

#define USERNAME_MAX 32
#define PASSWORD_MAX 64

typedef enum {
    VITA_LOGIN_IDLE,
    VITA_LOGIN_REGION_PICK,
    VITA_LOGIN_IME_USERNAME,
    VITA_LOGIN_IME_PASSWORD,
    VITA_LOGIN_PICK_ACTION,
    VITA_LOGIN_SUBMITTING,
} VitaLoginSubState;

static VitaLoginSubState sub_state = VITA_LOGIN_IDLE;
static int region_cursor = 0;
static int action_cursor = 0; // 0 = login, 1 = register
static bool ping_kicked = false;

static char username_utf8[USERNAME_MAX + 1] = { 0 };
static char password_utf8[PASSWORD_MAX + 1] = { 0 };

static SceWChar16 ime_title[64];
static SceWChar16 ime_buf[PASSWORD_MAX + 1];

static void utf8_to_utf16(const char* in, SceWChar16* out, size_t out_max) {
    size_t i = 0;
    for (; in[i] != '\0' && i + 1 < out_max; i++) {
        out[i] = (SceWChar16)(unsigned char)in[i];
    }
    out[i] = 0;
}

static void utf16_to_utf8(const SceWChar16* in, char* out, size_t out_max) {
    size_t i = 0;
    for (; in[i] != 0 && i + 1 < out_max; i++) {
        // BASIC_LATIN dialog only emits 0x20-0x7E; truncate the high byte.
        out[i] = (char)(in[i] & 0x7F);
    }
    out[i] = '\0';
}

// Gates vglSwapBuffers compositing of the Sce system dialog overlay.
bool vita_common_dialog_active = false;

static bool ime_module_loaded = false;

static bool ensure_ime_module(void) {
    if (ime_module_loaded) return true;
    int r = sceSysmoduleLoadModule(SCE_SYSMODULE_IME);
    Netplay_Log("VITALOGIN", "sceSysmoduleLoadModule(IME) rc=0x%08x", r);
    if (r < 0) return false;
    ime_module_loaded = true;
    return true;
}

static bool open_ime(const char* title_ascii, const char* initial, bool is_password) {
    if (!ensure_ime_module()) {
        Netplay_Log("VITALOGIN", "open_ime: module load failed");
        return false;
    }
    utf8_to_utf16(title_ascii, ime_title, sizeof(ime_title) / sizeof(ime_title[0]));

    SceImeDialogParam param;
    sceImeDialogParamInit(&param);
    param.supportedLanguages = 0;
    param.languagesForced = SCE_FALSE;
    param.type = SCE_IME_TYPE_BASIC_LATIN;
    param.option = is_password ? SCE_IME_OPTION_NO_AUTO_CAPITALIZATION : 0;
    param.title = ime_title;
    param.maxTextLength = is_password ? PASSWORD_MAX : USERNAME_MAX;
    param.inputTextBuffer = ime_buf;
    if (initial != NULL && initial[0] != '\0') {
        static SceWChar16 init_buf[PASSWORD_MAX + 1];
        utf8_to_utf16(initial, init_buf, sizeof(init_buf) / sizeof(init_buf[0]));
        param.initialText = init_buf;
    }
    SDL_zeroa(ime_buf);

    int rc = sceImeDialogInit(&param);
    Netplay_Log("VITALOGIN", "sceImeDialogInit rc=0x%08x title='%s' max=%u",
                rc, title_ascii, (unsigned)param.maxTextLength);
    if (rc < 0) return false;
    vita_common_dialog_active = true;
    return true;
}

static bool poll_ime_finished(char* out_utf8, size_t out_max) {
    SceCommonDialogStatus status = sceImeDialogGetStatus();
    if (status != SCE_COMMON_DIALOG_STATUS_FINISHED) {
        return false;
    }
    SceImeDialogResult res;
    SDL_zero(res);
    sceImeDialogGetResult(&res);
    sceImeDialogTerm();
    vita_common_dialog_active = false;

    if (res.button != SCE_IME_DIALOG_BUTTON_ENTER) {
        out_utf8[0] = '\0';
        return true;
    }
    utf16_to_utf8(ime_buf, out_utf8, out_max);
    return true;
}

// ---- Region picker ----

static void render_region_picker(void) {
    SSPutStrPro(1, 384, 50, 9, COLOR_WHITE, "3SX Online Setup");
    SSPutStrPro(1, 384, 70, 9, COLOR_GRAY, "Choose a matchmaking region:");

    const int count = Regions_Count();
    for (int i = 0; i < count; i++) {
        const Region* r = Regions_Get(i);
        if (r == NULL) continue;
        char line[96];
        if (r->ping_ms >= 0.0f) {
            SDL_snprintf(line, sizeof(line), "%-28s  %3.0f ms", r->label, r->ping_ms);
        } else if (Regions_IsPinging()) {
            SDL_snprintf(line, sizeof(line), "%-28s  pinging...", r->label);
        } else {
            SDL_snprintf(line, sizeof(line), "%-28s   ---", r->label);
        }
        const u32 col = (i == region_cursor) ? COLOR_YELLOW : COLOR_WHITE;
        SSPutStrPro(1, 384, 100 + i * 18, 9, col, line);
    }

    SSPutStrPro(1, 384, 180, 9, COLOR_GRAY, "D-PAD: navigate");
    SSPutStrPro(1, 384, 192, 9, COLOR_GRAY, "X / O / SQUARE: select");
    SSPutStrPro(1, 384, 204, 9, COLOR_GRAY, "TRIANGLE / START: cancel");
}

static void tick_region_picker(void) {
    if (!ping_kicked) {
        Regions_PingAllAsync();
        ping_kicked = true;
    }

    const u16 pressed = io_w.data[0].sw_new;
    if (pressed & SWK_UP) {
        region_cursor = (region_cursor - 1 + Regions_Count()) % Regions_Count();
    }
    if (pressed & SWK_DOWN) {
        region_cursor = (region_cursor + 1) % Regions_Count();
    }
    if (pressed & CONFIRM_BUTTONS) {
        Regions_Select(region_cursor);
        Netplay_Log("VITALOGIN", "picked region=%d", region_cursor);
        sub_state = VITA_LOGIN_PICK_ACTION;
        action_cursor = 0;
    }
    if (pressed & CANCEL_BUTTONS) {
        sub_state = VITA_LOGIN_IDLE;
    }
}

// ---- Login / Register action picker ----

static void render_action_picker(void) {
    SSPutStrPro(1, 384, 70, 9, COLOR_WHITE, "Account");

    const char* labels[2] = { "Log in",  "Create new account" };
    for (int i = 0; i < 2; i++) {
        const u32 col = (i == action_cursor) ? COLOR_YELLOW : COLOR_WHITE;
        char line[64];
        SDL_snprintf(line, sizeof(line), "%s %s",
                     (i == action_cursor) ? ">" : " ", labels[i]);
        SSPutStrPro(1, 384, 110 + i * 18, 9, col, line);
    }

    SSPutStrPro(1, 384, 160, 9, COLOR_GRAY, "First time? Pick \"Create new account\".");
    SSPutStrPro(1, 384, 180, 9, COLOR_GRAY, "D-PAD UP/DOWN: navigate");
    SSPutStrPro(1, 384, 192, 9, COLOR_GRAY, "X / O / SQUARE: confirm");
    SSPutStrPro(1, 384, 204, 9, COLOR_GRAY, "TRIANGLE / START: back");
}

static void tick_action_picker(void) {
    const u16 pressed = io_w.data[0].sw_new;
    if (pressed & SWK_UP) {
        action_cursor = (action_cursor - 1 + 2) % 2;
    }
    if (pressed & SWK_DOWN) {
        action_cursor = (action_cursor + 1) % 2;
    }
    if (pressed & CONFIRM_BUTTONS) {
        if (open_ime("Username", "", false)) {
            sub_state = VITA_LOGIN_IME_USERNAME;
        }
    }
    if (pressed & CANCEL_BUTTONS) {
        sub_state = VITA_LOGIN_REGION_PICK;
    }
}

// ---- IME username + password ----

static void render_ime_overlay(const char* prompt) {
    SSPutStrPro(1, 384, 100, 9, COLOR_WHITE, prompt);
    SSPutStrPro(1, 384, 140, 9, COLOR_GRAY, "Use the on-screen keyboard.");
    SSPutStrPro(1, 384, 192, 9, COLOR_GRAY, "TRIANGLE / START: cancel");
}

static void abort_ime_back_to_action(void) {
    sceImeDialogTerm();
    vita_common_dialog_active = false;
    sub_state = VITA_LOGIN_PICK_ACTION;
}

static void tick_ime_username(void) {
    const u16 pressed = io_w.data[0].sw_new;
    if (pressed & CANCEL_BUTTONS) {
        abort_ime_back_to_action();
        return;
    }
    if (!poll_ime_finished(username_utf8, sizeof(username_utf8))) {
        return;
    }
    if (username_utf8[0] == '\0') {
        sub_state = VITA_LOGIN_PICK_ACTION;
        return;
    }
    if (open_ime("Password", "", true)) {
        sub_state = VITA_LOGIN_IME_PASSWORD;
    } else {
        sub_state = VITA_LOGIN_PICK_ACTION;
    }
}

static void tick_ime_password(void) {
    const u16 pressed = io_w.data[0].sw_new;
    if (pressed & CANCEL_BUTTONS) {
        abort_ime_back_to_action();
        return;
    }
    if (!poll_ime_finished(password_utf8, sizeof(password_utf8))) {
        return;
    }
    if (password_utf8[0] == '\0') {
        sub_state = VITA_LOGIN_PICK_ACTION;
        return;
    }
    if (action_cursor == 0) {
        Fistbump_LoginDirect(username_utf8, password_utf8);
    } else {
        Fistbump_Register(username_utf8, password_utf8);
    }
    sub_state = VITA_LOGIN_SUBMITTING;
    Netplay_Log("VITALOGIN", "submitted auth user=%s mode=%s",
                username_utf8, action_cursor == 0 ? "login" : "register");
}

static void render_submitting(void) {
    SSPutStrPro(1, 384, 110, 9, COLOR_WHITE, "Logging in...");
    const char* err = Fistbump_GetLastError();
    if (err != NULL && err[0] != '\0') {
        SSPutStrPro(1, 384, 140, 9, 0xFF4040FF, err);
        SSPutStrPro(1, 384, 192, 9, COLOR_GRAY, "Press any face button to retry");
    }
}

static void tick_submitting(void) {
    const FistbumpState fs = Fistbump_GetState();
    if (fs == FISTBUMP_AWAITING_CREDENTIALS) {
        const u16 pressed = io_w.data[0].sw_new;
        if (pressed & CONFIRM_BUTTONS) {
            Fistbump_ClearError();
            sub_state = VITA_LOGIN_PICK_ACTION;
        }
        return;
    }
    if (fs != FISTBUMP_LOGGING_IN) {
        sub_state = VITA_LOGIN_IDLE;
    }
}

// ---- Public surface ----

void VitaLogin_Init(void) {
    sub_state = VITA_LOGIN_IDLE;
    region_cursor = Regions_GetSelectedIdx();
    action_cursor = 0;
    ping_kicked = false;
    SDL_zeroa(username_utf8);
    SDL_zeroa(password_utf8);
}

void VitaLogin_Tick(void) {
    const FistbumpState fs = Fistbump_GetState();

    if (fs == FISTBUMP_CONNECTING || fs == FISTBUMP_SENDING_TOKEN ||
        fs == FISTBUMP_LOGGING_IN || fs == FISTBUMP_AWAITING_LOGIN ||
        fs == FISTBUMP_AWAITING_CREDENTIALS || Fistbump_GetRoom() != NULL) {
        Fistbump_Run();
    }

    if (fs == FISTBUMP_AWAITING_CREDENTIALS && sub_state == VITA_LOGIN_IDLE) {
        sub_state = VITA_LOGIN_REGION_PICK;
        region_cursor = Regions_GetSelectedIdx();
        ping_kicked = false;
    }

    switch (sub_state) {
    case VITA_LOGIN_REGION_PICK:    tick_region_picker(); break;
    case VITA_LOGIN_PICK_ACTION:    tick_action_picker(); break;
    case VITA_LOGIN_IME_USERNAME:   tick_ime_username();  break;
    case VITA_LOGIN_IME_PASSWORD:   tick_ime_password();  break;
    case VITA_LOGIN_SUBMITTING:     tick_submitting();    break;
    case VITA_LOGIN_IDLE:           break;
    }
}

void VitaLogin_Render(void) {
    switch (sub_state) {
    case VITA_LOGIN_REGION_PICK:    render_region_picker(); break;
    case VITA_LOGIN_PICK_ACTION:    render_action_picker(); break;
    case VITA_LOGIN_IME_USERNAME:   render_ime_overlay("Enter username"); break;
    case VITA_LOGIN_IME_PASSWORD:   render_ime_overlay("Enter password"); break;
    case VITA_LOGIN_SUBMITTING:     render_submitting(); break;
    case VITA_LOGIN_IDLE:           break;
    }
}

#endif
