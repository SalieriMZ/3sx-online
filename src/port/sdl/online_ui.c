#include "port/sdl/online_ui.h"

#if NETPLAY_ENABLED

#include "args.h"
#include "platform/netplay/fistbump.h"
#include "platform/netplay/netplay.h"
#include "platform/netplay/netplay_log.h"
#include "platform/netplay/regions.h"
#include "port/config/config.h"
#include "port/creds.h"
#include "port/sdl/text_input.h"
#include "sf33rd/AcrSDK/common/pad.h"
#include "sf33rd/Source/Game/io/ioconv.h"
#include "sf33rd/Source/Game/ui/sc_sub.h"
#include "types.h"

#include <SDL3/SDL.h>

#define COLOR_WHITE  0xFFFFFFFF
#define COLOR_YELLOW 0xFFFF00FF
#define COLOR_GRAY   0x808080FF
#define COLOR_GREEN  0x40FF66FF
#define COLOR_RED    0xFF4040FF

#ifndef BUILD_VERSION
#define BUILD_VERSION "dev"
#endif

// Input is read from io_w.sw[] — the SWK_*-layout state ioconv produces and the
// SF3 menus use — NOT io_w.data[].sw_new, which is the raw FLPAD layout where the
// START/BACK bits are swapped relative to the SWK_* constants (ioconv_table maps
// flpad 0x8000->SWK_START 0x4000 and flpad 0x4000->SWK_BACK 0x8000). Reading the
// raw layout with SWK_* masks made Enter cancel and Backspace confirm. With the
// converted layout the standard semantics hold: confirm = SWK_SOUTH (keyboard J /
// face button) + SWK_START (Enter); cancel = SWK_EAST (K) + SWK_BACK (Backspace).
#define CONFIRM_BUTTONS (SWK_SOUTH | SWK_START)
#define CANCEL_BUTTONS  (SWK_EAST | SWK_BACK)

#define USERNAME_MAX 31
#define PASSWORD_MAX 63
#define ROOMNAME_MAX 31
#define ROOMCODE_MAX 15

typedef enum {
    OUI_LOGIN_ACTION, // Log in / Create account / Region / Remember-pw
    OUI_REGION,       // region picker (from login or hub)
    OUI_HUB,          // logged in, idle, no room
    OUI_ROOM,         // in a room
    OUI_STATUS,       // connecting / authenticating (passive)
    OUI_SEARCHING,    // AWAITING_MATCH
    OUI_MATCHED,      // accept / decline
    OUI_ERROR,        // connection error + retry
    OUI_UPDATE,       // in-game "update available" notice
} OnlineUISubState;

typedef enum {
    PT_NONE,
    PT_USERNAME,
    PT_PASSWORD,
    PT_ROOMNAME,
    PT_ROOMCODE,
} PendingText;

static bool visible = false;
static bool exit_requested = false;
static OnlineUISubState sub = OUI_STATUS;
static PendingText pending_text = PT_NONE;

static int region_cursor = 0;
static int action_cursor = 0; // 0=login, 1=register (within OUI_LOGIN_ACTION rows 0/1)
static int hub_cursor = 0;
static int room_cursor = 0;

// In-game update notice: raised once per menu-open when the server reports a
// newer release; update_return_sub is the screen to restore on dismiss.
static bool update_popup_shown = false;
static OnlineUISubState update_return_sub = OUI_LOGIN_ACTION;

static char username_buf[USERNAME_MAX + 1] = { 0 };
static char password_buf[PASSWORD_MAX + 1] = { 0 };
static char roomname_buf[ROOMNAME_MAX + 1] = { 0 };
static char roomcode_buf[ROOMCODE_MAX + 1] = { 0 };
static char ui_error[96] = { 0 };  // client-side validation error (cleared on retry)

static bool remember_pw = false;
static bool ping_kicked = false;
static bool auto_region_done = false;

// Edge-detected, SWK-layout pad input for the current frame (both pads OR'd),
// computed once per OnlineUI_Tick from io_w.sw[].
static u16 g_pad_prev = 0;
static u16 g_pad_pressed = 0;

// Free-running frame counter for the signed-in glow pulse.
static u32 g_frame = 0;

// ---------------------------------------------------------------------------
//  Small render/input helpers
// ---------------------------------------------------------------------------

static void put(u16 y, u32 col, const char* s) {
    SSPutStrPro(1, 384, y, 9, col, s);
}

// Centered on an arbitrary x (for corner text).
static void put_x(u16 x, u16 y, u32 col, const char* s) {
    SSPutStrPro(1, x, y, 9, col, s);
}

static void draw_list(const char** items, int count, int cursor, u16 y0, u16 dy) {
    for (int i = 0; i < count; i++) {
        char line[96];
        SDL_snprintf(line, sizeof(line), "%s %s", (i == cursor) ? ">" : " ", items[i]);
        put((u16)(y0 + i * dy), (i == cursor) ? COLOR_YELLOW : COLOR_WHITE, line);
    }
}

static void nav_cursor(int* cursor, int count) {
    if (g_pad_pressed & SWK_UP) {
        *cursor = (*cursor - 1 + count) % count;
    }
    if (g_pad_pressed & SWK_DOWN) {
        *cursor = (*cursor + 1) % count;
    }
}

static bool pressed_confirm(void) { return (g_pad_pressed & CONFIRM_BUTTONS) != 0; }
static bool pressed_cancel(void)  { return (g_pad_pressed & CANCEL_BUTTONS) != 0; }

static int force_relay_on(void) {
    const Args* a = get_args();
    return (a != NULL) && (a->netplay.force_relay != 0);
}

static void open_text(PendingText which, const char* title, const char* initial, bool is_pw, int max_len) {
    if (TextInput_Open(title, initial, is_pw, max_len)) {
        pending_text = which;
    }
}

// ---------------------------------------------------------------------------
//  Region picker
// ---------------------------------------------------------------------------

static void render_region(void) {
    put(72, COLOR_WHITE, "Choose matchmaking region");
    const int count = Regions_Count();
    for (int i = 0; i < count; i++) {
        const Region* r = Regions_Get(i);
        if (r == NULL) {
            continue;
        }
        char label[160];
        Regions_FormatLabel(r, label, sizeof(label));
        char line[176];
        SDL_snprintf(line, sizeof(line), "%s %s", (i == region_cursor) ? ">" : " ", label);
        put((u16)(100 + i * 18), (i == region_cursor) ? COLOR_YELLOW : COLOR_WHITE, line);
    }
    put(204, COLOR_GRAY, "Confirm: select   Cancel: back");
}

static void tick_region(void) {
    if (!ping_kicked) {
        Regions_PingAllAsync();
        ping_kicked = true;
    }
    const int count = Regions_Count();
    if (count > 0) {
        nav_cursor(&region_cursor, count);
    }
    if (pressed_confirm() && count > 0) {
        Regions_Select(region_cursor); // persists + reconnects on the new host
        Netplay_Log("ONLINEUI", "region select idx=%d", region_cursor);
        sub = Fistbump_IsLoggedIn() ? OUI_HUB : OUI_LOGIN_ACTION;
    } else if (pressed_cancel()) {
        sub = Fistbump_IsLoggedIn() ? OUI_HUB : OUI_LOGIN_ACTION;
    }
}

// ---------------------------------------------------------------------------
//  Login / register
// ---------------------------------------------------------------------------

static void render_login(void) {
    put(72, COLOR_WHITE, "3SX Online");
    put(92, COLOR_GRAY, "An account is required for online play.");

    char relabel[64];
    SDL_snprintf(relabel, sizeof(relabel), "Remember password: %s", remember_pw ? "ON" : "OFF");
    const char* items[4] = { "Log in", "Create new account", "Change region", relabel };
    draw_list(items, 4, action_cursor, 118, 18);

    const char* err = Fistbump_GetLastError();
    if (ui_error[0] != '\0') {
        put(190, COLOR_RED, ui_error);
    } else if (err != NULL && err[0] != '\0') {
        put(190, COLOR_RED, err);
    }
    put(206, COLOR_GRAY, "Confirm: select   Cancel: back to title");
}

static void tick_login(void) {
    nav_cursor(&action_cursor, 4);
    if (pressed_confirm()) {
        switch (action_cursor) {
        case 0: // login
        case 1: // register
            Fistbump_ClearError();
            ui_error[0] = '\0';
            open_text(PT_USERNAME, "Username", username_buf, false, USERNAME_MAX);
            break;
        case 2:
            region_cursor = Regions_GetSelectedIdx();
            sub = OUI_REGION;
            break;
        case 3:
            remember_pw = !remember_pw;
            Config_SetBool(CFG_KEY_NETPLAY_REMEMBER_PW, remember_pw);
            if (!remember_pw) {
                Creds_ClearPassword();
            }
            break;
        }
    } else if (pressed_cancel()) {
        exit_requested = true; // bail to title
    }
}

// ---------------------------------------------------------------------------
//  Hub (logged in, idle, no room)
// ---------------------------------------------------------------------------

enum { HUB_RANKED, HUB_CASUAL, HUB_CREATE, HUB_JOIN, HUB_REGION, HUB_RELAY, HUB_LOGOUT, HUB_COUNT };

static void render_hub(void) {
    const Region* cur = Regions_Get(Regions_GetSelectedIdx());
    char title[96];
    SDL_snprintf(title, sizeof(title), "Signed in as %s   [%s]", Fistbump_GetUsername(),
                 (cur != NULL && cur->code[0]) ? cur->code : "?");
    // Glow: pulse the title green -> white on a ~2s triangle wave so it "shines".
    const int phase = (int)(g_frame % 120);
    const int tri = (phase < 60) ? phase : (120 - phase); // 0..60
    const u32 lift = (u32)((tri * 255) / 60);             // 0..255
    const u32 rr = 0x40u + (0xBFu * lift) / 255u;         // 0x40..0xFF
    const u32 bb = 0x66u + (0x99u * lift) / 255u;         // 0x66..0xFF
    const u32 glow = (rr << 24) | (0xFFu << 16) | (bb << 8) | 0xFFu; // RRGGBBAA
    put(62, glow, title);

    char relay[40];
    SDL_snprintf(relay, sizeof(relay), "Force relay: %s", force_relay_on() ? "ON" : "OFF");
    const char* items[HUB_COUNT] = {
        "Find ranked match", "Find casual match", "Create private room",
        "Join private room", "Change region", relay, "Log out",
    };
    draw_list(items, HUB_COUNT, hub_cursor, 84, 16);
    put(202, COLOR_GRAY, "Confirm: select   Cancel: back");
}

static void tick_hub(void) {
    nav_cursor(&hub_cursor, HUB_COUNT);
    if (!pressed_confirm()) {
        if (pressed_cancel()) {
            exit_requested = true;
        }
        return;
    }
    switch (hub_cursor) {
    case HUB_RANKED:
        Fistbump_QueueMode("ranked");
        break;
    case HUB_CASUAL:
        Fistbump_QueueMode("casual");
        break;
    case HUB_CREATE:
        roomname_buf[0] = '\0';
        open_text(PT_ROOMNAME, "Room name (optional)", "", false, ROOMNAME_MAX);
        break;
    case HUB_JOIN:
        roomcode_buf[0] = '\0';
        open_text(PT_ROOMCODE, "Room code", "", false, ROOMCODE_MAX);
        break;
    case HUB_REGION:
        region_cursor = Regions_GetSelectedIdx();
        sub = OUI_REGION;
        break;
    case HUB_RELAY: {
        const int v = force_relay_on() ? 0 : 1;
        Args_SetForceRelay(v);
        Config_SetBool(CFG_KEY_NETPLAY_FORCE_RELAY, v != 0);
        Fistbump_SetForceRelay(v);
        break;
    }
    case HUB_LOGOUT:
        Fistbump_Logout();
        break;
    }
}

// ---------------------------------------------------------------------------
//  Room
// ---------------------------------------------------------------------------

// Room action rows: 0=Slot A, 1=Slot B, 2=Start(host)/spacer, 3=Leave.
static void render_room(void) {
    const Fistbump_Room* rm = Fistbump_GetRoom();
    if (rm == NULL) {
        return;
    }
    char hdr[96];
    SDL_snprintf(hdr, sizeof(hdr), "Room %s   (host: %s)", rm->code,
                 rm->host_name[0] ? rm->host_name : "?");
    put(50, COLOR_GREEN, hdr);

    char mem[48];
    SDL_snprintf(mem, sizeof(mem), "Members: %d/%d", rm->members, FISTBUMP_ROOM_MAX_MEMBERS);
    put(64, COLOR_GRAY, mem);

    u16 y = 78;
    for (int i = 0; i < rm->members; i++) {
        const char* mn = rm->member_names[i];
        const bool slotted = (SDL_strcmp(mn, rm->slot_a_name) == 0) || (SDL_strcmp(mn, rm->slot_b_name) == 0);
        char line[64];
        SDL_snprintf(line, sizeof(line), "%s - %d win%s", mn, rm->member_wins[i],
                     rm->member_wins[i] == 1 ? "" : "s");
        put(y, slotted ? COLOR_YELLOW : COLOR_WHITE, line);
        y += 13;
    }

    const char* sa = (rm->slot_a_name[0] && rm->slot_a_name[0] != '-') ? rm->slot_a_name : "(empty)";
    const char* sb = (rm->slot_b_name[0] && rm->slot_b_name[0] != '-') ? rm->slot_b_name : "(empty)";
    char rowa[64], rowb[64];
    SDL_snprintf(rowa, sizeof(rowa), "%s Slot A: %s", room_cursor == 0 ? ">" : " ", sa);
    SDL_snprintf(rowb, sizeof(rowb), "%s Slot B: %s", room_cursor == 1 ? ">" : " ", sb);
    put(160, room_cursor == 0 ? COLOR_YELLOW : COLOR_WHITE, rowa);
    put(174, room_cursor == 1 ? COLOR_YELLOW : COLOR_WHITE, rowb);

    if (rm->is_host) {
        const bool can_start = !rm->match_in_progress && sa[0] != '(' && sb[0] != '(';
        char start[40];
        SDL_snprintf(start, sizeof(start), "%s %s", room_cursor == 2 ? ">" : " ",
                     rm->match_in_progress ? "Match in progress..." : "Start match");
        put(188, room_cursor == 2 ? COLOR_YELLOW : (can_start ? COLOR_WHITE : COLOR_GRAY), start);
    } else if (rm->match_in_progress) {
        put(188, COLOR_GRAY, "Match in progress (host running).");
    } else {
        put(188, COLOR_GRAY, "Waiting for host to start.");
    }

    char leave[32];
    SDL_snprintf(leave, sizeof(leave), "%s Leave room", room_cursor == 3 ? ">" : " ");
    put(202, room_cursor == 3 ? COLOR_YELLOW : COLOR_WHITE, leave);
}

static void tick_room(void) {
    const Fistbump_Room* rm = Fistbump_GetRoom();
    if (rm == NULL) {
        return;
    }
    nav_cursor(&room_cursor, 4);
    if (pressed_cancel()) {
        Fistbump_RoomLeave();
        return;
    }
    if (!pressed_confirm()) {
        return;
    }
    switch (room_cursor) {
    case 0: // Slot A
        if (rm->is_slot_a) {
            Fistbump_RoomUnslot();
        } else if (rm->slot_a_name[0] == '\0' || rm->slot_a_name[0] == '-') {
            Fistbump_RoomSlot('A');
        }
        break;
    case 1: // Slot B
        if (rm->is_slot_b) {
            Fistbump_RoomUnslot();
        } else if (rm->slot_b_name[0] == '\0' || rm->slot_b_name[0] == '-') {
            Fistbump_RoomSlot('B');
        }
        break;
    case 2: // Start: host only, both slots filled, no match already running
        if (rm->is_host && !rm->match_in_progress
            && rm->slot_a_name[0] && rm->slot_a_name[0] != '-'
            && rm->slot_b_name[0] && rm->slot_b_name[0] != '-') {
            Fistbump_RoomStart();
        }
        break;
    case 3:
        Fistbump_RoomLeave();
        break;
    }
}

// ---------------------------------------------------------------------------
//  Passive status screens
// ---------------------------------------------------------------------------

static void render_status(void) {
    const FistbumpState fs = Fistbump_GetState();
    switch (fs) {
    case FISTBUMP_CONNECTING:
    case FISTBUMP_SENDING_TOKEN:
        put(120, COLOR_WHITE, "Connecting to server...");
        break;
    case FISTBUMP_LOGGING_IN:
    case FISTBUMP_AWAITING_LOGIN:
        put(120, COLOR_WHITE, "Authenticating...");
        break;
    case FISTBUMP_AWAITING_CREDENTIALS:
        put(120, COLOR_WHITE, "Sign in...");
        break;
    case FISTBUMP_IDLE:
        if (Regions_Count() <= 0) {
            put(110, COLOR_RED, "No regions configured.");
            put(132, COLOR_GRAY, "Add resources/regions.txt and restart.");
        } else {
            put(110, COLOR_GRAY, "Offline.");
            put(132, COLOR_GRAY, "Confirm: connect   Cancel: back");
        }
        break;
    default:
        break;
    }
}

static void render_searching(void) {
    put(130, COLOR_WHITE, "Finding match...");
    put(160, COLOR_GRAY, "Cancel: stop search");
}

static void render_matched(void) {
    const MatchResult* r = Fistbump_GetResult();
    // Restores the Vita-style "match found" screen: opponent name + the
    // ACCEPT / DECLINE button-glyph images (only reached for ranked/casual now;
    // room matches auto-accept). The leading spaces leave room for the glyphs.
    put(130, COLOR_WHITE, "Matched with:");
    if (r != NULL) {
        put(140, COLOR_GREEN, r->opponent_name);
    }
    put(160, COLOR_WHITE, "      ACCEPT      DECLINE");
    dispButtonImage2(121, 157, 0x19, 0x13, 0xF, 0, 4);
    dispButtonImage2(193, 157, 0x19, 0x13, 0xF, 0, 5);
}

static void render_error(void) {
    put(110, COLOR_RED, "Connection error");
    const char* err = Fistbump_GetLastError();
    if (err != NULL && err[0] != '\0') {
        put(132, COLOR_RED, err);
    }
    put(170, COLOR_GRAY, "Confirm: retry   Cancel: back to title");
}

static void render_update(void) {
    put(96, COLOR_YELLOW, "Update available");
    char line[80];
    SDL_snprintf(line, sizeof(line), "Latest: %s    You have: %s",
                 Fistbump_GetLatestVersion(), BUILD_VERSION);
    put(126, COLOR_WHITE, line);
    put(166, COLOR_GRAY, "Confirm: open download page    Cancel: later");
}

static void render_text_entry(void) {
    const char* prompt = "Enter text";
    bool pw = false;
    switch (pending_text) {
    case PT_USERNAME: prompt = "Enter username"; break;
    case PT_PASSWORD: prompt = "Enter password"; pw = true; break;
    case PT_ROOMNAME: prompt = "Enter room name (optional)"; break;
    case PT_ROOMCODE: prompt = "Enter room code"; break;
    default: break;
    }
    put(100, COLOR_WHITE, prompt);
    // On SDL the field echoes here; on Vita the system OSK shows its own field.
    const char* shown = TextInput_GetDisplay();
    if (shown[0] != '\0') {
        put(128, COLOR_YELLOW, shown);
    } else {
        put(128, COLOR_GRAY, pw ? "(typing...)" : "(use the on-screen keyboard)");
    }
    if (ui_error[0] != '\0') {
        put(150, COLOR_RED, ui_error);
    }
#if defined(__ANDROID__)
    // The soft keyboard has no Enter/Esc keys, so drive OK / Cancel / re-open the
    // keyboard with the pad (X / O / Triangle). See OnlineUI_Tick.
    put(180, COLOR_WHITE, "X: OK     O: Cancel     Triangle: keyboard");
#else
    put(196, COLOR_GRAY, "Enter: confirm   Esc: cancel");
#endif
}

// ---------------------------------------------------------------------------
//  Text-entry result handling
// ---------------------------------------------------------------------------

// Client-side checks mirroring the server (is_valid_username 3-16 + [a-z0-9_],
// password >= 6) so the user gets the error before submitting and can retry.
static const char* validate_username(const char* u) {
    const int n = (int)SDL_strlen(u);
    if (n < 3 || n > 16) {
        return "Username must be 3-16 characters";
    }
    for (int i = 0; i < n; i++) {
        const char c = u[i];
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) {
            return "Use only a-z, 0-9, _ (lowercase)";
        }
    }
    return NULL;
}

static const char* validate_password(const char* p) {
    if ((int)SDL_strlen(p) < 6) {
        return "Password must be at least 6 characters";
    }
    return NULL;
}

static void handle_text_result(TextInputStatus st, const char* value) {
    const PendingText which = pending_text;
    pending_text = PT_NONE;

    if (st != TEXT_INPUT_DONE) {
        // Cancelled: drop back to a sensible screen.
        sub = Fistbump_IsLoggedIn() ? OUI_HUB : OUI_LOGIN_ACTION;
        return;
    }

    switch (which) {
    case PT_USERNAME: {
        if (value[0] == '\0') {
            sub = OUI_LOGIN_ACTION;
            break;
        }
        const char* verr = validate_username(value);
        if (verr != NULL) {
            SDL_strlcpy(ui_error, verr, sizeof(ui_error));
            SDL_strlcpy(username_buf, value, sizeof(username_buf)); // keep for editing
            open_text(PT_USERNAME, "Username", username_buf, false, USERNAME_MAX);
            if (pending_text != PT_USERNAME) {
                sub = OUI_LOGIN_ACTION;
            }
            break;
        }
        ui_error[0] = '\0';
        SDL_strlcpy(username_buf, value, sizeof(username_buf));
        open_text(PT_PASSWORD, "Password", remember_pw ? password_buf : "", true, PASSWORD_MAX);
        if (pending_text != PT_PASSWORD) {
            sub = OUI_LOGIN_ACTION; // open failed
        }
        break;
    }
    case PT_PASSWORD: {
        if (value[0] == '\0') {
            sub = OUI_LOGIN_ACTION;
            break;
        }
        const char* verr = validate_password(value);
        if (verr != NULL) {
            SDL_strlcpy(ui_error, verr, sizeof(ui_error));
            open_text(PT_PASSWORD, "Password", "", true, PASSWORD_MAX);
            if (pending_text != PT_PASSWORD) {
                sub = OUI_LOGIN_ACTION;
            }
            break;
        }
        ui_error[0] = '\0';
        SDL_strlcpy(password_buf, value, sizeof(password_buf));
        if (remember_pw) {
            Creds_SavePassword(password_buf);
        }
        if (action_cursor == 1) {
            Fistbump_Register(username_buf, password_buf);
        } else {
            Fistbump_LoginDirect(username_buf, password_buf);
        }
        Netplay_Log("ONLINEUI", "submit auth user=%s mode=%s", username_buf,
                    action_cursor == 1 ? "register" : "login");
        sub = OUI_STATUS;
        break;
    }
    case PT_ROOMNAME:
        SDL_strlcpy(roomname_buf, value, sizeof(roomname_buf));
        Fistbump_RoomCreate(roomname_buf);
        sub = OUI_STATUS;
        break;
    case PT_ROOMCODE:
        if (value[0] == '\0') {
            sub = OUI_HUB;
            break;
        }
        SDL_strlcpy(roomcode_buf, value, sizeof(roomcode_buf));
        Fistbump_RoomJoin(roomcode_buf);
        sub = OUI_STATUS;
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
//  State sync: pick the screen from the fistbump state
// ---------------------------------------------------------------------------

static void sync_sub(void) {
    // The update notice sits over the idle screen until dismissed — don't let
    // connection-state sync pull us out of it.
    if (sub == OUI_UPDATE) {
        return;
    }
    const FistbumpState fs = Fistbump_GetState();
    const Fistbump_Room* room = Fistbump_GetRoom();

    switch (fs) {
    case FISTBUMP_AWAITING_CREDENTIALS:
        // Stay in any login sub-screen; otherwise (re)enter the action list.
        if (sub != OUI_LOGIN_ACTION && sub != OUI_REGION) {
            sub = OUI_LOGIN_ACTION;
        }
        break;
    case FISTBUMP_CONNECTING:
    case FISTBUMP_SENDING_TOKEN:
    case FISTBUMP_LOGGING_IN:
    case FISTBUMP_AWAITING_LOGIN:
        sub = OUI_STATUS;
        break;
    case FISTBUMP_AWAITING_MATCH:
        sub = OUI_SEARCHING;
        break;
    case FISTBUMP_MATCHED:
        sub = OUI_MATCHED;
        break;
    case FISTBUMP_ERROR:
        sub = OUI_ERROR;
        break;
    case FISTBUMP_IDLE:
        if (!Fistbump_IsLoggedIn()) {
            sub = OUI_STATUS;
        } else if (room != NULL) {
            if (sub != OUI_ROOM) {
                sub = OUI_ROOM;
                room_cursor = 0;
            }
        } else if (sub != OUI_HUB && sub != OUI_REGION) {
            sub = OUI_HUB;
            hub_cursor = 0;
        }
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
//  Public surface
// ---------------------------------------------------------------------------

void OnlineUI_Init(void* sdl_window) {
    TextInput_Init(sdl_window);
    visible = false;
    exit_requested = false;
    sub = OUI_STATUS;
    pending_text = PT_NONE;
    region_cursor = Regions_GetSelectedIdx();
    action_cursor = 0;
    hub_cursor = 0;
    room_cursor = 0;

    const char* saved = Config_GetString(CFG_KEY_NETPLAY_USERNAME);
    if (saved != NULL && saved[0] != '\0') {
        SDL_strlcpy(username_buf, saved, sizeof(username_buf));
    }
    remember_pw = Config_GetBool(CFG_KEY_NETPLAY_REMEMBER_PW);
    if (remember_pw) {
        Creds_LoadPassword(password_buf, sizeof(password_buf));
    }
}

void OnlineUI_Show(void) {
    visible = true;
    exit_requested = false;
    auto_region_done = false;
    region_cursor = Regions_GetSelectedIdx();
    action_cursor = 0;
    hub_cursor = 0;
    room_cursor = 0;
    roomname_buf[0] = '\0';
    roomcode_buf[0] = '\0';
    sub = OUI_STATUS;
    update_popup_shown = false;
    // Kick region pings on menu entry so the auto-best-region pick in
    // OnlineUI_Tick has data without the user opening the picker first.
    Regions_PingAllAsync();
    ping_kicked = true;
}

void OnlineUI_Hide(void) {
    visible = false;
    if (TextInput_IsActive()) {
        TextInput_Cancel();
        TextInput_Poll(NULL, 0);
    }
    pending_text = PT_NONE;
}

bool OnlineUI_IsVisible(void) {
    return visible;
}

bool OnlineUI_IsCapturingInput(void) {
    if (!visible) {
        return false;
    }
    const FistbumpState fs = Fistbump_GetState();
    return fs != FISTBUMP_GAME_START && fs != FISTBUMP_SENDING_UDP;
}

bool OnlineUI_ConsumeExitRequest(void) {
    if (!exit_requested) {
        return false;
    }
    exit_requested = false;
    return true;
}

void OnlineUI_HandleSDLEvent(const void* sdl_event) {
    TextInput_HandleSDLEvent(sdl_event);
}

void OnlineUI_Tick(void) {
    // Single shared pump gate so the matchmaking/room TCP socket can't be
    // starved (the historical CANCEL hang). Runs regardless of visibility.
    if (Fistbump_WantsPump()) {
        Fistbump_Run();
    }

    // Edge-detect this frame's pad presses in the SWK_* layout (both pads OR'd),
    // from io_w.sw[] (the converted layout) — see the CONFIRM_BUTTONS note. Done
    // every frame so the edge stays correct across show/hide + text entry.
    const u16 cur_sw = (u16)(io_w.sw[0] | io_w.sw[1]);
    g_pad_pressed = (u16)(cur_sw & ~g_pad_prev);
    g_pad_prev = cur_sw;
    g_frame++;

    if (!visible) {
        return;
    }

    const FistbumpState fs = Fistbump_GetState();
    if (fs == FISTBUMP_GAME_START || fs == FISTBUMP_SENDING_UDP) {
        return; // gameplay/handoff — draw + capture nothing
    }

    // Text entry takes over input while a field is open. The backend owns
    // commit/cancel (keyboard Enter/Esc on the SDL path, the OSK buttons on
    // Vita) — do NOT also read the pad here, or Backspace (SWK_BACK = the delete
    // key) and Enter (SWK_START) would double as cancel/confirm and fight the
    // field.
    if (TextInput_IsActive()) {
#if defined(__ANDROID__)
        // The soft keyboard has no Enter/Esc, and once dismissed it won't come
        // back on its own — so the pad drives OK / Cancel / re-open. Safe on
        // Android: typed characters arrive via SDL_TEXT_INPUT, NOT the keymap,
        // so these pad bits never collide with what's being typed. (On PC the
        // keymap WOULD carry typed letters as SWK_* — there we rely on Enter/Esc.)
        if (g_pad_pressed & SWK_SOUTH) {
            TextInput_Commit();
        } else if (g_pad_pressed & SWK_EAST) {
            TextInput_Cancel();
        } else if (g_pad_pressed & SWK_NORTH) {
            TextInput_ReopenKeyboard();
        }
#endif
        char value[PASSWORD_MAX + 1];
        const TextInputStatus st = TextInput_Poll(value, sizeof(value));
        if (st != TEXT_INPUT_PENDING) {
            handle_text_result(st, value);
        }
        return;
    }

    sync_sub();

    // Auto-pick the lowest-ping region once after the menu opens (ports the old
    // login_panel behaviour). Only pre-login on the login/status screen — never
    // while logged in, on the region picker, searching, matched, or in a room,
    // since Regions_Select reconnects (Fistbump_Reset + Start) and would drop a
    // live session/match.
    if (!auto_region_done && ping_kicked && !Regions_IsPinging()
        && !Fistbump_IsLoggedIn() && sub != OUI_REGION) {
        int best = -1;
        float best_ms = 1e9f;
        for (int i = 0; i < Regions_Count(); i++) {
            const Region* r = Regions_Get(i);
            if (r != NULL && r->ping_ms >= 0.0f && r->ping_ms < best_ms) {
                best_ms = r->ping_ms;
                best = i;
            }
        }
        if (best >= 0 && best != Regions_GetSelectedIdx()) {
            Regions_Select(best);
        }
        auto_region_done = true;
    }

    // The VERSION reply lands a few frames after connect; the first time it
    // reports a newer release, raise the update notice over the idle screen.
    if (!update_popup_shown && Fistbump_UpdateChecked() && Fistbump_UpdateAvailable()
        && (sub == OUI_LOGIN_ACTION || sub == OUI_REGION || sub == OUI_HUB)) {
        update_return_sub = sub;
        update_popup_shown = true;
        sub = OUI_UPDATE;
    }

    switch (sub) {
    case OUI_LOGIN_ACTION: tick_login(); break;
    case OUI_REGION:       tick_region(); break;
    case OUI_HUB:          tick_hub(); break;
    case OUI_ROOM:         tick_room(); break;
    case OUI_SEARCHING:
        if (pressed_cancel()) {
            Fistbump_CancelQueue();
        }
        break;
    case OUI_MATCHED:
        if (pressed_confirm()) {
            Fistbump_AcceptMatch();
        } else if (pressed_cancel()) {
            Fistbump_DeclineMatch();
        }
        break;
    case OUI_ERROR:
        if (pressed_confirm()) {
            Regions_Select(Regions_GetSelectedIdx()); // retry: reconnect
        } else if (pressed_cancel()) {
            exit_requested = true;
        }
        break;
    case OUI_STATUS:
        // Offline (e.g. after Logout): confirm reconnects in place; the old
        // login_panel had an explicit "Connect" here.
        if (pressed_confirm() && Fistbump_GetState() == FISTBUMP_IDLE
            && !Fistbump_IsLoggedIn() && Regions_Count() > 0) {
            Regions_Select(Regions_GetSelectedIdx());
        } else if (pressed_cancel()) {
            exit_requested = true;
        }
        break;
    case OUI_UPDATE:
        if (pressed_confirm()) {
            SDL_OpenURL(Fistbump_GetUpdateURL()); // opens the GitHub releases page
            sub = update_return_sub;
        } else if (pressed_cancel()) {
            sub = update_return_sub;
        }
        break;
    }
}

void OnlineUI_Render(void) {
    if (!visible) {
        return;
    }
    const FistbumpState fs = Fistbump_GetState();
    if (fs == FISTBUMP_GAME_START) {
        return; // gameplay
    }
    if (fs == FISTBUMP_SENDING_UDP) {
        // NAT-punch handoff: brief connecting feedback, no input (the menu also
        // breaks on this state so nothing acts).
        put(140, COLOR_WHITE, "Connecting...");
        return;
    }

    if (TextInput_IsActive()) {
        render_text_entry();
        return;
    }

    switch (sub) {
    case OUI_LOGIN_ACTION: render_login(); break;
    case OUI_REGION:       render_region(); break;
    case OUI_HUB:          render_hub(); break;
    case OUI_ROOM:         render_room(); break;
    case OUI_STATUS:       render_status(); break;
    case OUI_SEARCHING:    render_searching(); break;
    case OUI_MATCHED:      render_matched(); break;
    case OUI_ERROR:        render_error(); break;
    case OUI_UPDATE:       render_update(); break;
    }

    // Version footer, bottom-right: the 3SX client build (with the in-game update
    // status once the server has answered) + the connected broker's version.
    char vbuf[48];
    if (Fistbump_UpdateChecked() && Fistbump_UpdateAvailable()) {
        SDL_snprintf(vbuf, sizeof(vbuf), "3SX %s (update!)", BUILD_VERSION);
        put_x(612, 196, COLOR_YELLOW, vbuf);
    } else if (Fistbump_UpdateChecked()) {
        SDL_snprintf(vbuf, sizeof(vbuf), "3SX %s (latest)", BUILD_VERSION);
        put_x(612, 196, COLOR_GREEN, vbuf);
    } else {
        SDL_snprintf(vbuf, sizeof(vbuf), "3SX %s", BUILD_VERSION);
        put_x(612, 196, COLOR_GRAY, vbuf);
    }
    const char* sv = Fistbump_GetServerVersion();
    SDL_snprintf(vbuf, sizeof(vbuf), "srv %s", (sv != NULL && sv[0]) ? sv : "?");
    put_x(612, 210, COLOR_GRAY, vbuf);
}

#endif // NETPLAY_ENABLED
