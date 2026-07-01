#if NETPLAY_ENABLED

#include "platform/netplay/netplay.h"
#include "main.h"
#include "platform/app/sdl/sdl_app.h"
#include "platform/netplay/fistbump.h"
#include "platform/netplay/game_state.h"
#include "platform/netplay/sdl_net_adapter.h"
#include "platform/netplay/relay_adapter.h"
#include "platform/netplay/regions.h"
#include "port/paths.h"
#include "sf33rd/Source/Game/effect/effect.h"
#include "sf33rd/Source/Game/engine/grade.h"
#include "sf33rd/Source/Game/engine/plcnt.h"
#include "sf33rd/Source/Game/engine/workuser.h"
#include "sf33rd/Source/Game/game.h"
#include "sf33rd/Source/Game/io/gd3rd.h"
#include "sf33rd/Source/Game/io/pulpul.h"
#include "sf33rd/Source/Game/menu/menu.h"
#include "sf33rd/Source/Game/sound/se.h"
#include "sf33rd/Source/Game/rendering/mmtmcnt.h"
#include "sf33rd/Source/Game/rendering/color3rd.h"
#include "sf33rd/Source/Game/rendering/dc_ghost.h"
#include "sf33rd/Source/Game/rendering/mtrans.h"
#include "sf33rd/Source/Game/rendering/texcash.h"
#include "sf33rd/Source/Game/system/sys_sub.h"
#include "sf33rd/Source/Game/ui/sc_sub.h"
#include "port/sdl/netplay_screen.h"
#include "platform/netplay/netplay_log.h"
#include "platform/netplay/netplay_trace.h"
#include "platform/netplay/discord_rpc.h"
#include <time.h>
#include "sf33rd/Source/Game/system/work_sys.h"
#include "sf33rd/utils/djb2_hash.h"
#include "types.h"

#include <stdbool.h>

#include "gekkonet.h"
#include <SDL3/SDL.h>
#include <SDL3_net/SDL_net.h>

#include <stdio.h>
#include <stdlib.h>

#define INPUT_HISTORY_MAX 120
#define FRAME_SKIP_TIMER_MAX 60 // Allow skipping a frame roughly every second
#define STATS_UPDATE_TIMER_MAX 60
// Input delay floor. Observed rollback depth on cross-region relay was 8-10
// frames per render frame at ~250 ms RTT, which is enough to cause severe
// visual artifacts (any global state not in GS_SAVE drifts on every rollback).
// 3 frames = ~50 ms one-way buffer, absorbs ~100 ms RTT before rollback fires.
// SF3 parry is 1-frame so we still keep the floor as low as is workable.
#define DELAY_FRAMES 3
// Prediction/jitter budget in frames (~165ms at 60fps). Kept at 10 (the
// long-standing value): a load-induced pause or a late packet can briefly push
// frames_behind several frames, and the window has to absorb that without an
// unrecoverable overrun. Do not shrink this while loads can stall the sim.
#define INPUT_PREDICTION_WINDOW 10
#define PLAYER_COUNT 2

// Desync detection: GekkoNet checksum exchange + the per-frame full-state djb2
// + the diverged-frame state-buffer dump. Historically gated behind DEBUG, so
// every shipped (Release) build ran ZERO detection and desyncs were invisible
// in the field. Decoupled here so an instrumented Release can capture the exact
// GekkoDesyncDetected frame + dump. Define NETPLAY_DESYNC_DETECT=0 to compile it
// out where the cost matters (e.g. a perf-constrained Vita build).
#ifndef NETPLAY_DESYNC_DETECT
#define NETPLAY_DESYNC_DETECT 1
#endif

// Uncomment to enable packet drops
// #define LOSSY_ADAPTER

typedef struct EffectState {
    s16 frwctr;
    s16 frwctr_min;
    s16 head_ix[8];
    s16 tail_ix[8];
    s16 exec_tm[8];
    uintptr_t frw[EFFECT_MAX][448];
    s16 frwque[EFFECT_MAX];
} EffectState;

typedef struct State {
    GameState gs;
    EffectState es;
    LDREQ_Snapshot_t ld; // AFS load-request queue family (see gd3rd.h)
} State;

static GekkoSession* session = NULL;
static unsigned short local_port = 0;
static unsigned short remote_port = 0;
static const char* remote_ip = NULL;
static int player_number = 0;
static int player_handle = 0;

// Synctest (GekkoStressSession) mode: single-machine save-state validation.
// Active when FISTBUMP_SYNCTEST=<check_distance> is set at session start. Both
// actors are local; the stress session force-rolls-back every frame and
// compares full-state checksums, so any unsaved sim state desyncs offline.
static bool synctest_active = false;
static int synctest_check_distance = 0;
static int synctest_handles[2] = { 0, 0 };

// Roll the AFS load-request queue family (q_ldreq) back with the sim. OFF by
// default, and OFF is the fix: with the queue monotonic (the pre-1.8.0 behavior)
// a rollback never re-issues an already-completed load. Turning it ON re-creates
// the 1.8.0 mid-match reload storm + desync. Round/continue-transition desync is
// fixed instead by save-state completeness (ca_check_flag / sa_gauge_flash /
// chainex_check now live in GameState), NOT by rolling the queue back.
//
// A general "freeze the sim while afs_io_in_progress" was tried and REVERTED
// (commit 0997e9a): the match-start load runs in the STEPPED sim (Game_Task),
// so freezing it deadlocks at a black screen. Do NOT re-add it — see the note at
// run_netplay's synctest gate. Only the PEER's blocking load is paused, via
// fistbump_peer_loading. FISTBUMP_SAVE_LDREQ=1 rolls the queue back for A/B
// MEASUREMENT ONLY (it reintroduces the reload bug by construction); never ship.
static bool save_ldreq_enabled = false;

// Actual local input delay used this match (frames). Chosen per-match in
// configure_gekko (relay-aware, env-overridable) instead of a hardcoded
// constant, and reported in NetworkStats so the HUD reflects reality.
static int current_local_delay = DELAY_FRAMES;

// Run the full-state checksum / GekkoNet desync detection this match. OFF by
// default for normal play — the per-save 477KB djb2 (×rollback depth) is a real
// cost on a laggy link and only matters when actively hunting a desync. The
// q_ldreq save-state FIX that actually keeps peers in sync is separate and
// always on. Enabled when tracing, in synctest, or via FISTBUMP_DESYNC_DETECT=1.
static bool desync_detect_enabled = false;

static NetplaySessionState session_state = NETPLAY_SESSION_IDLE;
static char matched_ip[64];
static const char* matchmaking_server_ip = NULL;
static int matchmaking_server_port = 9000;
static bool matchmaking_pending = false;
static bool direct_p2p_pending = false;
static NET_DatagramSocket* p2p_sock = NULL;
static u16 input_history[2][INPUT_HISTORY_MAX] = { 0 };
static float frames_behind = 0;
static int frame_skip_timer = 0;
static int transition_ready_frames = 0;

// Post-match fast-return to the Network menu. Armed in EXITING only when a
// real match ran (gekko session existed); consumed by the dedicated
// transition at the end of EXITING. Voluntary menu EXIT keeps the title path.
static bool netplay_return_to_network = false;

static int stats_update_timer = 0;
static int frame_max_rollback = 0;
static NetworkStats network_stats = { 0 };

#if NETPLAY_DESYNC_DETECT
#define STATE_BUFFER_MAX 20

static State state_buffer[STATE_BUFFER_MAX] = { 0 };
#endif

#if defined(LOSSY_ADAPTER)
static GekkoNetAdapter* base_adapter = NULL;
static GekkoNetAdapter lossy_adapter = { 0 };

static float random_float() {
    return (float)rand() / RAND_MAX;
}

static void LossyAdapter_SendData(GekkoNetAddress* addr, const char* data, int length) {
    const float number = random_float();

    // Adjust this number to change drop probability
    if (number <= 0.25) {
        return;
    }

    base_adapter->send_data(addr, data, length);
}
#endif

static void clean_input_buffers() {
    p1sw_0 = 0;
    p2sw_0 = 0;
    p1sw_1 = 0;
    p2sw_1 = 0;
    p1sw_buff = 0;
    p2sw_buff = 0;
    SDL_zeroa(PLsw);
    SDL_zeroa(plsw_00);
    SDL_zeroa(plsw_01);
}

// Diagnostic: snapshot of state vars that gate the G_No[1] 12 -> 1 transition.
static void log_transition_state(const char* tag) {
    Netplay_Log("TRANS",
                "%s G_No=[%u,%u,%u,%u] mode=%d ss=%d "
                "t_game=%d t_menu=%d t_saver=%d t_entry=%d "
                "wipe=%u trf=%d pending=%d",
                tag,
                (unsigned)G_No[0], (unsigned)G_No[1],
                (unsigned)G_No[2], (unsigned)G_No[3],
                (int)Mode_Type, (int)session_state,
                task[TASK_GAME].condition, task[TASK_MENU].condition,
                task[TASK_SAVER].condition, task[TASK_ENTRY].condition,
                (unsigned)WipeLimit, transition_ready_frames,
                matchmaking_pending ? 1 : 0);
}

static void setup_vs_mode() {
    log_transition_state("setup_vs_mode/entry");
    task[TASK_MENU].r_no[0] = 5; // go to idle routine (doing nothing)
    cpExitTask(TASK_SAVER);

    plw[0].wu.operator = 1;
    plw[1].wu.operator = 1;
    Operator_Status[0] = 1;
    Operator_Status[1] = 1;
    grade_check_work_1st_init(0, 0);
    grade_check_work_1st_init(0, 1);
    grade_check_work_1st_init(1, 0);
    grade_check_work_1st_init(1, 1);
    Setup_Training_Difficulty();

    G_No[1] = 12;
    G_No[2] = 1;
    Mode_Type = MODE_NETWORK;
    cpExitTask(TASK_MENU);

    E_Timer = 0; // E_Timer can have different values depending on when the session was initiated

    Deley_Shot_No[0] = 0;
    Deley_Shot_No[1] = 0;
    Deley_Shot_Timer[0] = 15;
    Deley_Shot_Timer[1] = 15;
    Random_ix16 = 0;
    Random_ix32 = 0;
    Clear_Flash_Init(4);

    clean_input_buffers();
}

#if defined(LOSSY_ADAPTER)
static void configure_lossy_adapter(NET_DatagramSocket* sock) {
    base_adapter = SDLNetAdapter_Create(sock);
    lossy_adapter.send_data = LossyAdapter_SendData;
    lossy_adapter.receive_data = base_adapter->receive_data;
    lossy_adapter.free_data = base_adapter->free_data;
}
#endif

// Pick the local input delay for this match. Higher delay absorbs steady-state
// latency without rollback; too high hurts feel (SF3 parry is 1 frame). Relay
// adds a hop, so budget one extra frame there. FISTBUMP_DELAY_FRAMES overrides
// for playtesting. Kept per-match (set once at start) — GekkoNet's local delay
// is a start-of-session knob; true RTT-adaptive mid-match needs the START
// handshake to carry a measured peer RTT (a protocol change, deferred).
static int compute_local_delay(void) {
    const char* env = SDL_getenv("FISTBUMP_DELAY_FRAMES");
    if (env != NULL && env[0] != '\0') {
        int d = SDL_atoi(env);
        if (d < 1) d = 1;
        if (d > 10) d = 10;
        return d;
    }

    const MatchResult* mr = Fistbump_GetResult();
    if (mr != NULL && mr->use_relay) {
        return DELAY_FRAMES + 1;
    }
    return DELAY_FRAMES;
}

static void configure_gekko() {
    GekkoConfig config;
    SDL_zero(config);

    // Init the determinism trace (reads FISTBUMP_TRACE) and stamp what this
    // build actually runs, so a captured log is unambiguous about whether
    // detection/trace were active and which tuning was compiled in.
    Netplay_TraceInit();

    // Synctest: a GekkoStressSession drives the REAL local sim with a forced
    // rollback every frame (up to check_distance), comparing full-state
    // checksums — single machine, no peer, no net adapter. Any sim global that
    // isn't in the save-state desyncs offline and deterministically. Drive it
    // through round/continue transitions (character loads) to exercise the
    // q_ldreq path specifically. Enable with FISTBUMP_SYNCTEST=<check_distance>
    // (e.g. 7) and reach a match via the direct-P2P entry (--p2p-remote-ip).
    synctest_active = false;
    {
        const char* sc = SDL_getenv("FISTBUMP_SYNCTEST");
        if (sc != NULL && sc[0] != '\0') {
            synctest_check_distance = SDL_atoi(sc);
            if (synctest_check_distance < 1) {
                synctest_check_distance = 1;
            }
            synctest_active = true;
        }
    }

    {
        // Default OFF (monotonic queue). FISTBUMP_SAVE_LDREQ=1 rolls the queue
        // back with the sim again (the 1.8.0 behavior) for A/B testing only.
        const char* sl = SDL_getenv("FISTBUMP_SAVE_LDREQ");
        save_ldreq_enabled = (sl != NULL && sl[0] == '1');
    }

    {
        // Off for normal play; on when it's actually wanted (tracing needs the
        // checksum for cs=, synctest needs the compare, or explicit override).
        const char* dd = SDL_getenv("FISTBUMP_DESYNC_DETECT");
        desync_detect_enabled = synctest_active
                                || (Netplay_TraceLevel() > 0)
                                || (dd != NULL && dd[0] == '1');
    }

    current_local_delay = compute_local_delay();

    Netplay_Log("BUILD", "desync_detect=%d trace=%d synctest=%d save_ldreq=%d state_size=%u delay=%d pred=%d",
                desync_detect_enabled ? 1 : 0, Netplay_TraceLevel(),
                synctest_active ? synctest_check_distance : 0,
                save_ldreq_enabled ? 1 : 0,
                (unsigned)sizeof(State), current_local_delay, INPUT_PREDICTION_WINDOW);

    config.num_players = PLAYER_COUNT;
    config.input_size = sizeof(u16);
    config.state_size = sizeof(State);
    config.max_spectators = 0;
    config.input_prediction_window = INPUT_PREDICTION_WINDOW;

    if (synctest_active) {
        // desync_detection + the checksum compare are the whole point here, and
        // are mutually exclusive with limited_saving in GekkoNet.
        config.desync_detection = true;
        config.limited_saving = false;
        config.check_distance = (unsigned)synctest_check_distance;

        if (gekko_create(&session, GekkoStressSession)) {
            gekko_start(session, &config);
        }
        // Two LOCAL actors, no remote, no adapter.
        for (int i = 0; i < PLAYER_COUNT; i++) {
            synctest_handles[i] = gekko_add_actor(session, GekkoLocalPlayer, NULL);
            gekko_set_local_delay(session, synctest_handles[i], current_local_delay);
        }
        player_handle = synctest_handles[player_number];
        SDL_Log("Netplay: SYNCTEST stress session, check_distance=%d", synctest_check_distance);
        Netplay_Log("GEKKO", "synctest stress session check_distance=%d", synctest_check_distance);
        return;
    }

#if NETPLAY_DESYNC_DETECT
    config.desync_detection = desync_detect_enabled;
#endif

    if (gekko_create(&session, GekkoGameSession)) {
        gekko_start(session, &config);
    } else {
        SDL_Log("Netplay: gekko session already running");
    }

    NET_DatagramSocket* mm_sock = Fistbump_GetSocket();
    NET_DatagramSocket* active_sock;
    if (mm_sock != NULL) {
        // Matchmaking path: reuse the socket that was registered with the server.
        active_sock = mm_sock;
    } else {
        // Direct P2P path: create a dedicated UDP socket on local_port.
        NET_Init();
        p2p_sock = NET_CreateDatagramSocket(NULL, local_port);
        active_sock = p2p_sock;
    }

    const MatchResult* mr = Fistbump_GetResult();
    if (mr != NULL && mr->use_relay) {
        gekko_net_adapter_set(session,
            RelayAdapter_Create(active_sock,
                                mr->relay_ip,
                                mr->relay_port,
                                mr->match_id,
                                mr->player));
    } else {
#if defined(LOSSY_ADAPTER)
        configure_lossy_adapter(active_sock);
        gekko_net_adapter_set(session, &lossy_adapter);
#else
        gekko_net_adapter_set(session, SDLNetAdapter_Create(active_sock));
#endif
    }

    SDL_Log("Netplay: starting session for player %d at port %hu", player_number, local_port);

    char remote_address_str[100];
    if (mr != NULL && mr->use_relay) {
        int other_side = (mr->player == 1) ? 2 : 1;
        SDL_snprintf(remote_address_str, sizeof(remote_address_str), "peer-%d", other_side);
    } else {
        SDL_snprintf(remote_address_str, sizeof(remote_address_str), "%s:%hu", remote_ip, remote_port);
    }
    GekkoNetAddress remote_address = { .data = remote_address_str, .size = strlen(remote_address_str) };

    for (int i = 0; i < PLAYER_COUNT; i++) {
        const bool is_local_player = (i == player_number);

        if (is_local_player) {
            player_handle = gekko_add_actor(session, GekkoLocalPlayer, NULL);
            gekko_set_local_delay(session, player_handle, current_local_delay);
        } else {
            gekko_add_actor(session, GekkoRemotePlayer, &remote_address);
        }
    }
}

static u16 get_inputs() {
    // The game doesn't differentiate between controllers and players.
    // That's why we OR the inputs of both local controllers together to get
    // local inputs.
    u16 inputs = 0;
    inputs = p1sw_buff | p2sw_buff;
    return inputs;
}

static void note_input(u16 input, int player, int frame) {
    if (frame < 0) {
        return;
    }

    input_history[player][frame % INPUT_HISTORY_MAX] = input;
}

static u16 recall_input(int player, int frame) {
    if (frame < 0) {
        return 0;
    }

    return input_history[player][frame % INPUT_HISTORY_MAX];
}

#if NETPLAY_DESYNC_DETECT
static uint32_t calculate_checksum(const State* state) {
    uint32_t hash = djb2_init();
    hash = djb2_updatep(hash, state);
    return hash;
}

/// Zero out all pointers in WORK for dumping
static void clean_work_pointers(WORK* work) {
    work->target_adrs = NULL;
    work->hit_adrs = NULL;
    work->dmg_adrs = NULL;
    work->suzi_offset = NULL;
    SDL_zeroa(work->char_table);
    work->se_random_table = NULL;
    work->step_xy_table = NULL;
    work->move_xy_table = NULL;
    work->overlap_char_tbl = NULL;
    work->olc_ix_table = NULL;
    work->rival_catch_tbl = NULL;
    work->curr_rca = NULL;
    work->set_char_ad = NULL;
    work->hit_ix_table = NULL;
    work->body_adrs = NULL;
    work->h_bod = NULL;
    work->hand_adrs = NULL;
    work->h_han = NULL;
    work->dumm_adrs = NULL;
    work->h_dumm = NULL;
    work->catch_adrs = NULL;
    work->h_cat = NULL;
    work->caught_adrs = NULL;
    work->h_cau = NULL;
    work->attack_adrs = NULL;
    work->h_att = NULL;
    work->h_eat = NULL;
    work->hosei_adrs = NULL;
    work->h_hos = NULL;
    work->att_ix_table = NULL;
    work->my_effadrs = NULL;

    work->current_colcd = 0;
    work->colcd = 0;
    work->extra_col = 0;
    work->extra_col_2 = 0;
}

static void clean_plw_pointers(PLW* plw) {
    clean_work_pointers(&plw->wu);
    plw->cp = NULL;
    plw->dm_step_tbl = NULL;
    plw->as = NULL;
    plw->sa = NULL;
    plw->cb = NULL;
    plw->py = NULL;
    plw->rp = NULL;
}

static void clean_state_pointers(State* state) {
    for (int i = 0; i < 2; i++) {
        clean_plw_pointers(&state->gs.plw[i]);

        for (int j = 0; j < 56; j++) {
            state->gs.waza_work[i][j].w_ptr = NULL;
        }

        state->gs.spg_dat[i].spgtbl_ptr = NULL;
        state->gs.spg_dat[i].spgptbl_ptr = NULL;
    }

    for (int i = 0; i < EFFECT_MAX; i++) {
        WORK* work = (WORK*)state->es.frw[i];
        clean_work_pointers(work);

        WORK_Other* work_big = (WORK_Other*)state->es.frw[i];
        work_big->my_master = NULL;
    }

    for (int i = 0; i < SDL_arraysize(state->gs.bg_w.bgw); i++) {
        state->gs.bg_w.bgw[i].bg_address = NULL;
        state->gs.bg_w.bgw[i].suzi_adrs = NULL;
        state->gs.bg_w.bgw[i].start_suzi = NULL;
        state->gs.bg_w.bgw[i].suzi_adrs2 = NULL;
        state->gs.bg_w.bgw[i].start_suzi2 = NULL;
        state->gs.bg_w.bgw[i].deff_rl = NULL;
        state->gs.bg_w.bgw[i].deff_plus = NULL;
        state->gs.bg_w.bgw[i].deff_minus = NULL;
    }

    state->gs.ci_pointer = NULL;

    for (int i = 0; i < SDL_arraysize(state->gs.task); i++) {
        state->gs.task[i].func_adrs = NULL;
    }

    // Exclude the q_ldreq load-queue snapshot from the cross-peer checksum. By
    // default the queue is NOT rolled back at all (save_ldreq off, monotonic) so
    // a rollback never re-issues a completed load, and the loaded RESULT lands in
    // GameState (which IS checksummed). These bytes are real-time AFS bookkeeping
    // (progress / completion / retry / byte-counts + raw result/lds pointers) the
    // netplay pump mutates out of band on wall-clock, so even with
    // FISTBUMP_SAVE_LDREQ=1 (queue rolled back for A/B) they must stay out of the
    // checksum or an in-flight load reads as a false desync (confirmed via the
    // synctest: gs/es matched, only ldcs diverged).
    SDL_zero(state->ld);
}

/// Save state in state buffer.
/// @return Pointer to state as it has been saved.
static const State* note_state(const State* state, int frame) {
    if (frame < 0) {
        frame += STATE_BUFFER_MAX;
    }

    State* dst = &state_buffer[frame % STATE_BUFFER_MAX];
    SDL_memcpy(dst, state, sizeof(State));
    clean_state_pointers(dst);
    return dst;
}

static void dump_state(const State* src, const char* filename) {
    SDL_IOStream* io = SDL_IOFromFile(filename, "w");
    SDL_WriteIO(io, src, sizeof(State));
    SDL_CloseIO(io);
}

static void dump_saved_state(int frame) {
    const State* src = &state_buffer[frame % STATE_BUFFER_MAX];

    char filename[100];
    SDL_snprintf(filename, sizeof(filename), "states/%d_%d", player_handle, frame);

    dump_state(src, filename);
}
#endif

#define SDL_copya(dst, src) SDL_memcpy(dst, src, sizeof(src))

static void gather_state(State* dst) {
    // GameState
    GameState* gs = &dst->gs;
    GameState_Save(gs);

    // EffectState
    EffectState* es = &dst->es;
    SDL_copya(es->frw, frw);
    SDL_copya(es->exec_tm, exec_tm);
    SDL_copya(es->frwque, frwque);
    SDL_copya(es->head_ix, head_ix);
    SDL_copya(es->tail_ix, tail_ix);
    es->frwctr = frwctr;
    es->frwctr_min = frwctr_min;

    // Load-request queue. Default (save_ldreq off) = MONOTONIC: leave dst->ld
    // zeroed so the queue is not rolled back — this is the fix (a rollback never
    // re-issues a completed load; round/continue convergence comes from
    // save-state completeness instead). FISTBUMP_SAVE_LDREQ=1 snapshots it so it
    // rolls back with the sim, which reintroduces the 1.8.0 reload storm — A/B
    // measurement only (see save_ldreq_enabled at the top of this file).
    if (save_ldreq_enabled) {
        LDREQ_Snapshot(&dst->ld);
    } else {
        SDL_zero(dst->ld);
    }
}

// Set in process_events for the most-recently-processed advance so the save
// that immediately follows it can tag the frame as a rollback re-sim. A
// divergence shows up as the same frame number logging two different `cs=`
// values across a rollback boundary.
static bool trace_advance_rolling_back = false;

// Gather the determinism-critical scalars from the live sim globals (exactly
// the state just saved) and hand them to the trace channel. No-op unless
// FISTBUMP_TRACE >= 1.
static void emit_trace_frame(int frame, uint32_t checksum,
                             uint32_t cs_gs, uint32_t cs_es, uint32_t cs_ld) {
    extern bool afs_io_in_progress;

    NetplayTraceFrame tf;
    SDL_zero(tf);
    tf.frame = frame;
    tf.checksum = checksum;
    tf.cs_gs = cs_gs;
    tf.cs_es = cs_es;
    tf.cs_ld = cs_ld;
    tf.rollbacks = trace_advance_rolling_back ? 1 : 0;
    tf.frames_behind = frames_behind;
    tf.round_num = Round_num;
    tf.wins[0] = PL_Wins[0];
    tf.wins[1] = PL_Wins[1];
    for (int i = 0; i < 4; i++) {
        tf.gno[i] = G_No[i];
        tf.cno[i] = C_No[i];
    }
    tf.rng_ix16 = Random_ix16;
    tf.rng_ix32 = Random_ix32;
    tf.rng_ix16_ex = Random_ix16_ex;
    tf.rng_ix32_ex = Random_ix32_ex;
    tf.ld_depth = Get_LDREQ_Depth();
    tf.afs_io = afs_io_in_progress ? 1 : 0;
    for (int i = 0; i < 2; i++) {
        tf.p_char[i] = My_char[i];
        tf.p_act[i] = plw[i].wu.cg_number;
        tf.p_charidx[i] = plw[i].wu.char_index;
        tf.p_x[i] = plw[i].wu.xyz[0].disp.pos;
        tf.p_y[i] = plw[i].wu.xyz[1].disp.pos;
        tf.p_vit[i] = plw[i].wu.vital_new;
    }
    Netplay_TraceFrame(&tf);
}

static void save_state(GekkoGameEvent* event) {
    *event->data.save.state_len = sizeof(State);
    State* dst = (State*)event->data.save.state;

    gather_state(dst);

    const int frame = event->data.save.frame;
    uint32_t checksum = 0, cs_gs = 0, cs_es = 0, cs_ld = 0;
#if NETPLAY_DESYNC_DETECT
    // The checksum + state-buffer copy are the expensive part (≈477KB hash/copy
    // per save, multiplied by rollback depth). Only do it when detection is
    // actually enabled this match — normal play skips it entirely.
    if (desync_detect_enabled) {
        const State* saved_state = note_state(dst, frame);
        checksum = calculate_checksum(saved_state);
        *event->data.save.checksum = checksum;
        if (Netplay_TraceOn()) {
            // Per-surface sub-checksums over the cleaned clone so a desync can be
            // localized to GameState / EffectState / the q_ldreq snapshot.
            cs_gs = djb2_update_mem(djb2_init(), (const uint8_t*)&saved_state->gs, sizeof(saved_state->gs));
            cs_es = djb2_update_mem(djb2_init(), (const uint8_t*)&saved_state->es, sizeof(saved_state->es));
            cs_ld = djb2_update_mem(djb2_init(), (const uint8_t*)&saved_state->ld, sizeof(saved_state->ld));
        }
    }
#endif

    if (Netplay_TraceOn()) {
        emit_trace_frame(frame, checksum, cs_gs, cs_es, cs_ld);
    }
}

static void load_state(const State* src) {
    // GameState
    const GameState* gs = &src->gs;
    GameState_Load(gs);

    // EffectState
    const EffectState* es = &src->es;
    SDL_copya(frw, es->frw);
    SDL_copya(exec_tm, es->exec_tm);
    SDL_copya(frwque, es->frwque);
    SDL_copya(head_ix, es->head_ix);
    SDL_copya(tail_ix, es->tail_ix);
    frwctr = es->frwctr;
    frwctr_min = es->frwctr_min;

    // Load-request queue (see gather_state). Restored only when save_ldreq is
    // ON; the default (OFF, monotonic = the fix) leaves the live queue untouched
    // so a rollback never re-issues a completed load.
    if (save_ldreq_enabled) {
        LDREQ_Restore(&src->ld);
    }
}

static void load_state_from_event(GekkoGameEvent* event) {
    const State* src = (State*)event->data.load.state;
    load_state(src);
}

static bool game_ready_to_run_character_select() {
    return G_No[1] == 1;
}

static bool need_to_catch_up() {
    return frames_behind >= 1;
}

// Per-frame flag — reset in begin_frame, set when step_game(render=true)
// or the offline draw path ran. Renderers read this to hold-prev-frame on
// stalls instead of clearing to black.
bool game_drew_this_frame = false;

static void step_game(bool render) {
    // render=false is a rollback / skipped tick: sim advances, no draw calls accumulate.
    No_Trans = !render;

    njUserMain();
    seqsBeforeProcess();
    if (render) {
        njdp2d_draw();
        game_drew_this_frame = true;
    }
    seqsAfterProcess();
}

static void advance_game(GekkoGameEvent* event, bool render) {
    const u16* inputs = (u16*)event->data.adv.inputs;
    const int frame = event->data.adv.frame;

    p1sw_0 = PLsw[0][0] = inputs[0];
    p2sw_0 = PLsw[1][0] = inputs[1];
    p1sw_1 = PLsw[0][1] = recall_input(0, frame - 1);
    p2sw_1 = PLsw[1][1] = recall_input(1, frame - 1);

    note_input(inputs[0], 0, frame);
    note_input(inputs[1], 1, frame);

    step_game(render);
}

static void handle_disconnection() {
    if (session_state == NETPLAY_SESSION_EXITING || session_state == NETPLAY_SESSION_IDLE) {
        return;
    }

    // Soft_Reset_Sub fires from the EXITING arm; tearing down gekko first
    // avoids a stuck-fade bug on the surviving peer.
    clean_input_buffers();
    session_state = NETPLAY_SESSION_EXITING;
}

static void process_session() {
    frames_behind = -gekko_frames_ahead(session);

    gekko_network_poll(session);

    u16 local_inputs = get_inputs();
    if (synctest_active) {
        // Both actors are local: drive p1 from the controller, leave p2 idle.
        // p1 acting (and KO'ing the idle p2) is enough to march through round /
        // continue transitions where the unsaved q_ldreq state is consulted.
        u16 idle = 0;
        gekko_add_local_input(session, synctest_handles[0], &local_inputs);
        gekko_add_local_input(session, synctest_handles[1], &idle);
    } else {
        gekko_add_local_input(session, player_handle, &local_inputs);
    }

    int session_event_count = 0;
    GekkoSessionEvent** session_events = gekko_session_events(session, &session_event_count);

    for (int i = 0; i < session_event_count; i++) {
        const GekkoSessionEvent* event = session_events[i];

        switch (event->type) {
        case GekkoPlayerSyncing:
            SDL_Log("Netplay: player syncing");
            Netplay_Log("GEKKO", "player syncing");
            // FIXME: Show status to the player
            break;

        case GekkoPlayerConnected:
            SDL_Log("Netplay: player connected");
            Netplay_Log("GEKKO", "player connected");
            break;

        case GekkoPlayerDisconnected:
            SDL_Log("Netplay: player disconnected");
            Netplay_Log("DISCONNECT", "gekko reported player disconnected");
            handle_disconnection();
            break;

        case GekkoSessionStarted:
            SDL_Log("Netplay: session started");
            Netplay_Log("GEKKO", "session started");
            session_state = NETPLAY_SESSION_RUNNING;
            break;

        case GekkoDesyncDetected: {
            const int frame = event->data.desynced.frame;
            const unsigned lc = event->data.desynced.local_checksum;
            const unsigned rc = event->data.desynced.remote_checksum;
            SDL_Log("Netplay: desync detected at frame %d (local=0x%08x remote=0x%08x)", frame, lc, rc);
            Netplay_Log("DESYNC", "frame=%d local=0x%08x remote=0x%08x synctest=%d",
                        frame, lc, rc, synctest_active ? 1 : 0);
            Netplay_TraceTrigger("DESYNC", "frame=%d local=0x%08x remote=0x%08x", frame, lc, rc);

#if NETPLAY_DESYNC_DETECT
            dump_saved_state(frame);
#endif
            break;
        }

        case GekkoEmptySessionEvent:
        case GekkoSpectatorPaused:
        case GekkoSpectatorUnpaused:
            // Do nothing
            break;
        }
    }
}

static void process_events(bool drawing_allowed) {
    int game_event_count = 0;
    GekkoGameEvent** game_events = gekko_update_session(session, &game_event_count);
    int frames_rolled_back = 0;
    int last_advance_frame = -1;

    for (int i = 0; i < game_event_count; i++) {
        const GekkoGameEvent* event = game_events[i];

        switch (event->type) {
        case GekkoLoadEvent:
            load_state_from_event(event);
            break;

        case GekkoAdvanceEvent: {
            const bool rolling_back = event->data.adv.rolling_back;
            trace_advance_rolling_back = rolling_back;
            advance_game(event, drawing_allowed && !rolling_back);
            frames_rolled_back += rolling_back ? 1 : 0;
            last_advance_frame = event->data.adv.frame;
            break;
        }

        case GekkoSaveEvent:
            save_state(event);
            break;

        case GekkoEmptyGameEvent:
            // Do nothing
            break;
        }
    }

    frame_max_rollback = SDL_max(frame_max_rollback, frames_rolled_back);

    if (last_advance_frame >= 0) {
        Netplay_LogPerFrame(last_advance_frame, frames_behind, frames_rolled_back);
    }
}

static void step_logic(bool drawing_allowed) {
    process_session();
    process_events(drawing_allowed);
}

static void update_network_stats() {
    if (stats_update_timer == 0) {
        GekkoNetworkStats net_stats;
        gekko_network_stats(session, player_handle ^ 1, &net_stats);

        network_stats.ping = net_stats.avg_ping;
        network_stats.delay = current_local_delay;

        if (frame_max_rollback < network_stats.rollback) {
            // Don't decrease the reading by more than a frame to account for
            // the opponent not pressing buttons for 1-2 seconds
            network_stats.rollback -= 1;
        } else {
            network_stats.rollback = frame_max_rollback;
        }

        frame_max_rollback = 0;
        stats_update_timer = STATS_UPDATE_TIMER_MAX;
    }

    stats_update_timer -= 1;
    stats_update_timer = SDL_max(stats_update_timer, 0);
}

static void run_netplay() {
    // Drain TCP so server-pushed CANCEL / LOADING / room broadcasts are processed mid-match.
    Fistbump_Run();

    extern bool fistbump_peer_cancelled;
    if (fistbump_peer_cancelled) {
        fistbump_peer_cancelled = false;
        handle_disconnection();
        return;
    }

    extern bool afs_io_in_progress;

    // Service the load queue outside the stepped sim whenever a load is
    // pending. The queue normally advances inside Game_Task frames, but in
    // netplay the sim stalls in two deadlock-prone ways: (a) we freeze on
    // fistbump_peer_loading below and starve our own loader, or (b) the PEER
    // froze on OUR loading signal, stops sending inputs, gekko starves, and
    // our in-sim loader never finishes — so our LOADING 0 never goes out and
    // the peer stays frozen forever. Pumping here breaks both cycles: the
    // load always completes in real time regardless of sim progress. Loaded
    // data is AFS/texture buffers, not sim state, so this cannot desync.
    extern void Check_LDREQ_Queue(void);
    if (afs_io_in_progress) {
        Check_LDREQ_Queue();
    }

    Fistbump_TickLoadingSignal(afs_io_in_progress);

    if (fistbump_peer_loading) {
        // Peer stalled on AFS — pause our advance to prevent rollback blow-out.
        update_network_stats();
        return;
    }

    // Synctest only: never let the stress session advance / roll back while an
    // AFS load is in flight (single machine, no peer to gate on).
    //
    // We do NOT do this in real netplay: a general "freeze the sim while
    // afs_io_in_progress" deadlocks the match-start / round-transition load,
    // because that load is driven by the STEPPED sim (Game_Task), not only the
    // out-of-band pump above — freezing the sim stalls it forever (black screen
    // at match start). Real netplay keeps the load queue MONOTONIC instead
    // (save_ldreq off, the pre-1.8.0 behavior) so a rollback never re-issues it,
    // and only pauses on the PEER's big blocking load via fistbump_peer_loading.
    if (synctest_active && afs_io_in_progress) {
        update_network_stats();
        return;
    }

    const bool catch_up = need_to_catch_up() && (frame_skip_timer == 0);
    step_logic(!catch_up);

    if (catch_up && session_state == NETPLAY_SESSION_RUNNING) {
        step_logic(true);
        frame_skip_timer = FRAME_SKIP_TIMER_MAX;
    }

    frame_skip_timer -= 1;
    frame_skip_timer = SDL_max(frame_skip_timer, 0);

    // Update stats

    update_network_stats();
}

void Netplay_SetParams(int player, const char* ip) {
    SDL_assert(player == 1 || player == 2);
    player_number = player - 1;
    remote_ip = ip;

    if (SDL_strcmp(ip, "127.0.0.1") == 0) {
        switch (player_number) {
        case 0:
            local_port = 50000;
            remote_port = 50001;
            break;

        case 1:
            local_port = 50001;
            remote_port = 50000;
            break;
        }
    } else {
        local_port = 50000;
        remote_port = 50000;
    }
}

void Netplay_BeginDirectP2P() {
    if (remote_ip == NULL) {
        return;
    }
    direct_p2p_pending = true;
}

void Netplay_TickDirectP2P() {
    if (!direct_p2p_pending) {
        return;
    }

    direct_p2p_pending = false;
    setup_vs_mode();

    SDL_zeroa(input_history);
    frames_behind = 0;
    frame_skip_timer = 0;
    transition_ready_frames = 0;

    session_state = NETPLAY_SESSION_TRANSITIONING;
}

void Netplay_SetMatchmakingParams(const char* server_ip, int server_port) {
    matchmaking_server_ip = server_ip;
    matchmaking_server_port = server_port;
}

void Netplay_BeginMatchmaking() {
    // Refresh region latencies every time the Network screen is entered so
    // the picker auto-selects the lowest-ping server with current data.
    Regions_PingAllAsync();
    if (matchmaking_server_ip == NULL) {
        return;
    }
    // Reuse the existing TCP if already authenticated so the server keeps our room membership.
    if (!Fistbump_IsLoggedIn() || Fistbump_GetConnectState() != FISTBUMP_CONN_CONNECTED) {
        Fistbump_Start(matchmaking_server_ip, matchmaking_server_port, FISTBUMP_LOCAL_UDP_PORT, Paths_GetPrefPath());
    }
    matchmaking_pending = true;
    log_transition_state("BeginMatchmaking");
}

void Netplay_TickMatchmaking() {
    if (!matchmaking_pending) {
        return;
    }

    Fistbump_Run();

    const FistbumpState mm = Fistbump_GetState();

    if (mm == FISTBUMP_GAME_START) {
        const MatchResult* r = Fistbump_GetResult();
        player_number = r->player - 1;
        SDL_strlcpy(matched_ip, r->ip, sizeof(matched_ip));
        remote_ip = matched_ip;
        remote_port = (unsigned short)r->remote_port;
        SDL_zeroa(input_history);
        frames_behind = 0;
        frame_skip_timer = 0;
        transition_ready_frames = 0;
        matchmaking_pending = false;
        setup_vs_mode();
        session_state = NETPLAY_SESSION_TRANSITIONING;
        log_transition_state("GAME_START/post-setup");
    } else if (mm == FISTBUMP_ERROR) {
        // Connection failed/refused. Stay in the Network menu — the login
        // overlay surfaces the error and the region picker can retry
        // (Regions_Select restarts fistbump). Bouncing through Soft_Reset_Sub
        // ejected the player to the main screen over a transient failure.
    }
}

bool Netplay_IsMatchmakingPending() {
    // Returns false once matched so cancel is ignored during the display countdown.
    return matchmaking_pending && Fistbump_GetState() != FISTBUMP_GAME_START;
}

void Netplay_FindMatch() {
    if (matchmaking_server_ip == NULL) {
        return;
    }

    if (!Fistbump_IsLoggedIn()) {
        return;
    }

    Fistbump_Queue();
}

void Netplay_CancelMatchmaking() {
    // In-room: preserve TCP + profile so we keep receiving ROOM STATE.
    // Out-of-room: full reset.
    if (Fistbump_GetRoom() != NULL) {
        Fistbump_EndMatch();
    } else {
        Fistbump_Reset();
    }
    matchmaking_pending = false;
}

void Netplay_Run() {
    switch (session_state) {
    case NETPLAY_SESSION_TRANSITIONING: {
        static int transitioning_log_counter = 0;
        if ((transitioning_log_counter++ % 60) == 0) {
            log_transition_state("TRANSITIONING/tick");
        }
        Fistbump_Run();

        extern bool fistbump_peer_cancelled;
        if (fistbump_peer_cancelled) {
            fistbump_peer_cancelled = false;
            handle_disconnection();
            break;
        }

        extern bool afs_io_in_progress;
        Fistbump_TickLoadingSignal(afs_io_in_progress);

        extern bool fistbump_peer_loading;
        // Same anti-starvation pump as run_netplay (see comment there).
        {
            extern bool afs_io_in_progress;
            extern void Check_LDREQ_Queue(void);
            if (afs_io_in_progress) {
                Check_LDREQ_Queue();
            }
        }
        if (fistbump_peer_loading) {
            update_network_stats();
            break;
        }
        if (game_ready_to_run_character_select()) {
            transition_ready_frames += 1;
        } else {
            transition_ready_frames = 0;
            clean_input_buffers();
            step_game(true);
        }

        if (transition_ready_frames >= 2) {
            configure_gekko();
            session_state = NETPLAY_SESSION_CONNECTING;
            log_transition_state("TRANSITIONING/done");
        }

        break;
    }

    case NETPLAY_SESSION_CONNECTING:
    case NETPLAY_SESSION_RUNNING:
        run_netplay();
        break;

    case NETPLAY_SESSION_EXITING:
        if (session != NULL) {
            netplay_return_to_network = true;
        }
        // Destroy gekko + adapter BEFORE Fistbump_Reset tears down the shared UDP socket.
        if (session != NULL) {
            gekko_destroy(&session);
            SDLNetAdapter_Destroy();
            RelayAdapter_Destroy();
        }

        if (p2p_sock != NULL) {
            NET_DestroyDatagramSocket(p2p_sock);
            p2p_sock = NULL;
            NET_Quit();
        }

        Netplay_CancelMatchmaking();

        Soft_Reset_Sub();

        // Soft_Reset_Sub calls FadeOut without FadeInit; reset state so the next FadeIn clears.
        Fade_Flag = 0;
        FadeLimit = 1;

        // Soft_Reset_Sub doesn't clear the operator flags set by setup_vs_mode.
        Operator_Status[0] = 0;
        Operator_Status[1] = 0;
        plw[0].wu.operator = 0;
        plw[1].wu.operator = 0;
        Mode_Type = MODE_ARCADE;

        // Clear netplay-side state so the next Network entry starts fresh.
        display_netplay_text = false;
        frames_behind = 0;
        frame_skip_timer = 0;
        frame_max_rollback = 0;
        transition_ready_frames = 0;
        matchmaking_pending = false;
        direct_p2p_pending = false;
        synctest_active = false;
        player_number = 0;
        player_handle = 0;
        remote_ip = NULL;
        remote_port = 0;
        local_port = 0;
        SDL_zero(network_stats);
        SDL_zeroa(input_history);
        SDL_zeroa(matched_ip);

        extern bool fistbump_peer_loading;
        extern bool fistbump_peer_cancelled;
        fistbump_peer_loading = false;
        fistbump_peer_cancelled = false;

        // Post-match return-to-Network: a dedicated transition that resets
        // exactly what the boot path (title -> mode select) would rebuild,
        // then lands the menu task on the Network routine. The pieces the
        // earlier shortcut missed — and which produced the effect-queue abort
        // and the mis-scaled viewport — are effect_work_init() and the MENU
        // texture set (Soft_Reset_Sub prepares the TITLE set, list 6; menus
        // need list 2, exactly what Game0_2 stages before entering menus).
        // Only armed when a real match ran (gekko session existed); a
        // voluntary EXIT from the Network menu keeps the classic title path.
        if (netplay_return_to_network) {
            netplay_return_to_network = false;

            // Cancel the cold-boot chain Soft_Reset_Sub armed and revive the
            // menu task (TASK_GAME/TASK_DEBUG were re-readied by the reset,
            // TASK_ENTRY never died).
            cpExitTask(TASK_INIT);
            cpReadyTask(TASK_MENU, Menu_Task);
            Forbid_Reset = 0;

            // What the title->menu path rebuilds (Game0_2 cases 3-5).
            Purge_mmtm_area(2);
            Make_texcash_of_list(2);
            effect_work_init();
            System_all_clear_Level_B();

            // Land on After_Title -> Netplay_Menu (same state set as
            // Back_to_Mode_Select, with the Network routine selected).
            G_No[0] = 2;
            G_No[1] = 12;
            G_No[2] = 0;
            G_No[3] = 0;
            E_No[0] = 1;
            E_No[1] = 2;
            E_No[2] = 2;
            E_No[3] = 0;
            Menu_Init(&task[TASK_MENU]);
            for (int rix = 0; rix < 4; rix++) {
                task[TASK_MENU].r_no[rix] = 0;
            }
            task[TASK_MENU].r_no[1] = 6; // After_Title -> Netplay_Menu
            BGM_Request_Code_Check(0x41);
        }

        session_state = NETPLAY_SESSION_IDLE;
        // Suppress title-screen attract-mode demo for the rest of the process —
        // hitting it after a netplay session segfaults on Windows + Android.
        extern bool demo_disabled_after_netplay;
        demo_disabled_after_netplay = true;
        log_transition_state("EXITING/done");
        break;

    case NETPLAY_SESSION_IDLE:
        break;
    }
}

NetplaySessionState Netplay_GetSessionState() {
    return session_state;
}

float Netplay_GetFramesBehind(void) {
    return frames_behind;
}

void Netplay_TickDiscord(void) {
    static int last_key = -1;       // hash of (mode_type, fistbump_state, in_room)
    static long long match_start_ts = 0;

    // Compute a coarse "current state" key. Bump when any of these change.
    const FistbumpState fs = Fistbump_GetState();
    const ModeType mt = Mode_Type;
    const bool room = (Fistbump_GetRoom() != NULL);
    int key = ((int)fs * 256) + ((int)mt * 4) + (room ? 1 : 0);
    if (key == last_key) return;
    last_key = key;

    // In-match: keep timestamp continuity across rollback-y frames.
    if (fs == FISTBUMP_GAME_START) {
        if (match_start_ts == 0) {
            match_start_ts = (long long)time(NULL);
        }
    } else {
        match_start_ts = 0;
    }

    const char* state = "In main menu";
    const char* details = NULL;
    long long start_ts = 0;

    if (fs == FISTBUMP_GAME_START) {
        const MatchResult* mr = Fistbump_GetResult();
        state = "In Match";
        details = (mr && mr->opponent_name[0]) ? mr->opponent_name : NULL;
        start_ts = match_start_ts;
    } else if (fs == FISTBUMP_AWAITING_MATCH) {
        state = "Searching for opponent";
    } else if (fs == FISTBUMP_MATCHED) {
        state = "Match found";
    } else if (room) {
        const Fistbump_Room* rm = Fistbump_GetRoom();
        state = "In private room";
        details = rm ? rm->code : NULL;
    } else {
        switch (mt) {
            case MODE_ARCADE:          state = "Arcade Mode"; break;
            case MODE_VERSUS:          state = "Versus (local 2P)"; break;
            case MODE_NORMAL_TRAINING: state = "Training"; break;
            case MODE_PARRY_TRAINING:  state = "Parry Training"; break;
            case MODE_REPLAY:          state = "Watching replay"; break;
            case MODE_NETWORK:         state = "Online"; break;
            default:                   state = "In main menu"; break;
        }
    }

    DiscordRPC_SetActivity(state, details, NULL, start_ts);
}

void Netplay_HandleMenuExit() {
    Netplay_CancelMatchmaking();

    switch (session_state) {
    case NETPLAY_SESSION_IDLE:
    case NETPLAY_SESSION_EXITING:
        break;

    case NETPLAY_SESSION_TRANSITIONING:
    case NETPLAY_SESSION_CONNECTING:
    case NETPLAY_SESSION_RUNNING:
        session_state = NETPLAY_SESSION_EXITING;
        break;
    }
}

void Netplay_GetNetworkStats(NetworkStats* stats) {
    SDL_copyp(stats, &network_stats);
}

#endif
