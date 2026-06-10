#if CRS_APP_DRIVER_SDL

#include "platform/app/sdl/sdl_app.h"
#include "arcade/arcade_balance.h"
#include "args.h"
#include "common.h"
#include "main.h"
#include "platform/video/sdl_generic/sdl_generic_renderer.h"
#include "port/config/config.h"
#include "port/config/keymap.h"
#include "port/sdl/sdl_debug_text.h"
#include "port/sdl/sdl_message_renderer.h"
#include "port/sound/adx.h"
#include "sf33rd/AcrSDK/ps2/foundaps2.h"

#if CRS_INPUT_DRIVER_SDL
#include "platform/input/sdl/sdl_pad.h"
#endif

#if NETPLAY_ENABLED
#include "platform/netplay/fistbump.h"
#include "platform/netplay/netplay.h"
#include "platform/netplay/netplay_log.h"
#include "platform/netplay/crash_log.h"
#include "platform/netplay/discord_rpc.h"
#include "platform/netplay/regions.h"
#include "port/sdl/netplay_screen.h"
#include "port/sdl/netstats_renderer.h"
#endif

#if DEBUG
#include "sf33rd/Source/Game/debug/debug_config.h"
#endif

#if IMGUI
#include "imgui/imgui_wrapper.h"
#include "imgui/settings_panel.h"
#include "imgui/chat_panel.h"
#if NETPLAY_ENABLED
#include "imgui/login_panel.h"
#endif
#include "imgui/dcimgui/dcimgui.h"
#endif

#if defined(__vita__) && NETPLAY_ENABLED
#include "platform/app/vita/vita_login.h"
#endif

#if STATCHECK
#include "test/test_runner.h"
#endif

#include "port/io/afs.h"
#include "port/paths.h"
#include "port/resources.h"

#include <SDL3/SDL.h>

#ifdef __ANDROID__
#include <android/log.h>
#endif
// SDL3's Android bootloader expects SDL_main(); SDL_main.h rewrites main().
#ifdef __ANDROID__
#include <SDL3/SDL_main.h>
#endif

#if _WIN32 && DEBUG
#include <windef.h>

#include <ConsoleApi.h>
#include <stdio.h>
#endif

typedef enum ScaleMode {
    SCALEMODE_NEAREST,
    SCALEMODE_SQUARE_PIXELS,
    SCALEMODE_INTEGER,
} ScaleMode;

typedef enum AppPhase {
    APP_PHASE_INIT,
    APP_PHASE_COPYING_RESOURCES,
    APP_PHASE_INITIALIZED,
} AppPhase;

static AppPhase phase = APP_PHASE_INIT;

static const char* app_name = "Street Fighter III: 3rd Strike";
static const float display_target_ratio = 4.0 / 3.0;
static const int window_min_width = 384;
static const int window_min_height = (int)(window_min_width / display_target_ratio);
static const Uint64 target_frame_time_ns = 1000000000.0 / TARGET_FPS;

static SDL_Window* window = NULL;
static ScaleMode scale_mode = SCALEMODE_NEAREST;

static Uint64 frame_deadline = 0;
static FrameMetrics frame_metrics = { 0 };
static Uint64 last_frame_end_time = 0;

static Uint64 last_mouse_motion_time = 0;
static const int mouse_hide_delay_ms = 2000; // 2 seconds

static void init_scalemode() {
#ifdef __vita__
    // NEAREST + LINEAR canvas sampler = largest readable 4:3 area on 960x544.
    scale_mode = SCALEMODE_NEAREST;
    return;
#endif
    const char* raw_scalemode = Config_GetString(CFG_KEY_SCALEMODE);

    if (raw_scalemode == NULL) {
        return;
    }

    if (SDL_strcmp(raw_scalemode, "nearest") == 0) {
        scale_mode = SCALEMODE_NEAREST;
    } else if (SDL_strcmp(raw_scalemode, "square-pixels") == 0) {
        scale_mode = SCALEMODE_SQUARE_PIXELS;
    } else if (SDL_strcmp(raw_scalemode, "integer") == 0) {
        scale_mode = SCALEMODE_INTEGER;
    }
}

static bool init_window() {
    SDL_WindowFlags window_flags = 0;
#ifdef __ANDROID__
    // RESIZABLE flips Android into FULL_USER orientation (overrides landscape).
    // HIGH_PIXEL_DENSITY trips Mali-G615 gralloc — leave both off on Android.
#else
    window_flags |= SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#endif

    if (Config_GetBool(CFG_KEY_FULLSCREEN)) {
        window_flags |= SDL_WINDOW_FULLSCREEN;
    }

    int window_width = Config_GetInt(CFG_KEY_WINDOW_WIDTH);
    window_width = SDL_max(window_width, window_min_width);

    int window_height = Config_GetInt(CFG_KEY_WINDOW_HEIGHT);
    window_height = SDL_max(window_height, window_min_height);

    window = SDLGenericRenderer_Init(&(SDLRenderBackendInitInfo) {
        .app_name = app_name,
        .window_width = window_width,
        .window_height = window_height,
        .window_flags = window_flags,
    });

    if (window == NULL) {
        return false;
    }

    return true;
}

static int pre_init() {
    SDL_SetAppMetadata(app_name, "0.1", NULL);
    SDL_SetHint(SDL_HINT_VIDEO_WAYLAND_PREFER_LIBDECOR, "1");
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
#ifdef __vita__
    // Use the dummy video driver so vitaGL owns gxm (avoids double-init).
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
#endif
#ifdef __ANDROID__
    // SF3 is a fixed-aspect 4:3 arcade output — lock to landscape.
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
#endif

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("Couldn't initialize SDL: %s", SDL_GetError());
        return 1;
    }

    return 0;
}

#if _WIN32 && DEBUG
static void init_windows_console() {
    // attaches to an existing console for printouts. Works with windows CMD but not MSYS2
    if (AttachConsole(ATTACH_PARENT_PROCESS) == 0) {
        // if fails, then allocate a new console
        AllocConsole();
    }
    freopen("CONIN$", "r", stdin);
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
}
#endif

static int full_init() {
    SDL_Log("full_init: enter");
    Config_Init();
    SDL_Log("full_init: Config_Init OK");
#if NETPLAY_ENABLED
    // Init before any SDL_Log so init crashes are captured.
    CrashLog_Init();
    SDL_Log("full_init: CrashLog_Init OK");
#endif
    Keymap_Init();
    init_scalemode();
    SDL_Log("full_init: Keymap+scalemode OK");

    if (!SDL_Init(SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        SDL_Log("Couldn't initialize SDL: %s", SDL_GetError());
        return 1;
    }
    SDL_Log("full_init: SDL_Init AUDIO|GAMEPAD OK");

    if (!init_window()) {
        SDL_Log("Couldn't initialize SDL window: %s", SDL_GetError());
        return 1;
    }
    SDL_Log("full_init: init_window OK");

// #if DEBUG
//     SDLDebugText_Initialize(renderer);
// #endif

// Initialize pads
#if CRS_INPUT_DRIVER_SDL
    SDLPad_Init();
#endif

#if _WIN32 && DEBUG
    init_windows_console();
#endif

    ArcadeBalance_Init();
#ifdef __vita__
    // Smaller chunk on Vita so AFS_OnSyncTick fires often enough to keep TCP drained.
    AFS_Init(Resources_GetAFSPath(), 64 * 1024);
#else
    AFS_Init(Resources_GetAFSPath(), 256 * 1024);
#endif

#if DEBUG
    DebugConfig_Init();
#endif

    Main_Init();

#if NETPLAY_ENABLED
    Netplay_LogInit();
#ifdef DISCORD_APP_ID
    DiscordRPC_Init(DISCORD_APP_ID);
    DiscordRPC_SetEnabled(Config_GetBool(CFG_KEY_DISCORD_RPC));
    DiscordRPC_SetActivity("In main menu", NULL, "logo", 0);
#else
    DiscordRPC_Init("");
    DiscordRPC_SetEnabled(false);
#endif

#if defined(__ANDROID__) || defined(__vita__)
    // Mobile/handheld: read persisted region + force-relay (no launcher CLI).
    Regions_LoadPersistedSelection();
    Args_LoadNetplayPrefs();
    const Region* persisted = Regions_Get(Regions_GetSelectedIdx());
    if (persisted != NULL) {
        Netplay_SetMatchmakingParams(persisted->ip, persisted->port);
    }
#endif
#ifdef __vita__
    VitaLogin_Init();
#endif
#endif

    return 0;
}

static void cleanup() {
    AFS_Finish();
    Config_Destroy();
    SDLGenericRenderer_Quit();
}

#if IMGUI
static void toggle_debug_window_visibility(SDL_KeyboardEvent* event) {
    if ((event->key == SDLK_GRAVE) && event->down && !event->repeat) {
        ImGuiW_ToggleVisivility();
    }
}

static void toggle_settings_panel(SDL_KeyboardEvent* event) {
    if (event->key == SDLK_F3 && event->down && !event->repeat) {
        if (!ImGui_GetIO()->WantTextInput) {
            SettingsPanel_Toggle();
        }
    }
}

static void toggle_chat_panel(SDL_KeyboardEvent* event) {
    if (event->key == SDLK_TAB && event->down && !event->repeat) {
        if (!ImGui_GetIO()->WantTextInput) {
            ChatPanel_Toggle();
        }
    }
}
#endif

static void handle_fullscreen_toggle(SDL_KeyboardEvent* event) {
    const bool is_alt_enter = (event->key == SDLK_RETURN) && (event->mod & SDL_KMOD_ALT);
    const bool is_f11 = (event->key == SDLK_F11);
    const bool correct_key = (is_alt_enter || is_f11);

    if (!correct_key || !event->down || event->repeat) {
        return;
    }

    const SDL_WindowFlags flags = SDL_GetWindowFlags(window);

    if (flags & SDL_WINDOW_FULLSCREEN) {
        SDL_SetWindowFullscreen(window, false);
    } else {
        SDL_SetWindowFullscreen(window, true);
    }
}

static void handle_mouse_motion() {
    last_mouse_motion_time = SDL_GetTicks();
    SDL_ShowCursor();
}

static void hide_cursor_if_needed() {
    const Uint64 now = SDL_GetTicks();

    if ((last_mouse_motion_time > 0) && ((now - last_mouse_motion_time) > mouse_hide_delay_ms)) {
        SDL_HideCursor();
    }
}

static bool poll_events() {
    SDL_Event event;
    bool continue_running = true;

    while (SDL_PollEvent(&event)) {
#if IMGUI
        ImGuiW_ProcessEvent(&event);
#endif

        switch (event.type) {
        case SDL_EVENT_GAMEPAD_ADDED:
        case SDL_EVENT_GAMEPAD_REMOVED:
#if CRS_INPUT_DRIVER_SDL
            SDLPad_HandleGamepadDeviceEvent(&event.gdevice);
#endif
            break;

        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
#if IMGUI
            toggle_debug_window_visibility(&event.key);
            toggle_settings_panel(&event.key);
            toggle_chat_panel(&event.key);
#endif

            handle_fullscreen_toggle(&event.key);
            break;

        case SDL_EVENT_MOUSE_MOTION:
            handle_mouse_motion();
            break;

        case SDL_EVENT_QUIT:
            continue_running = false;
            break;
        }
    }

    return continue_running;
}

static void begin_frame() {
#if STATCHECK
    TestRunner_Prologue();
#endif

#if IMGUI
    ImGuiW_NewFrame();
#endif

    AFS_RunServer();

#if NETPLAY_ENABLED
    extern bool game_drew_this_frame;
    game_drew_this_frame = false;
#endif
}

static void center_rect(SDL_Rect* rect, int win_w, int win_h) {
    rect->x = (win_w - rect->w) / 2;
    rect->y = (win_h - rect->h) / 2;
}

static SDL_Rect fit_4_by_3_rect(int win_w, int win_h) {
    SDL_Rect rect;
    rect.w = win_w;
    rect.h = (int)((float)win_w / display_target_ratio);

    if (rect.h > win_h) {
        rect.h = win_h;
        rect.w = (int)((float)win_h * display_target_ratio);
    }

    center_rect(&rect, win_w, win_h);
    return rect;
}

static SDL_Rect fit_integer_rect(int win_w, int win_h, int pixel_w, int pixel_h) {
    const int virtual_w = win_w / pixel_w;
    const int virtual_h = win_h / pixel_h;
    const int scale_w = virtual_w / 384;
    const int scale_h = virtual_h / 224;
    int scale = (scale_h < scale_w) ? scale_h : scale_w;

    // Better to show a cropped image than nothing at all
    if (scale < 1) {
        scale = 1;
    }

    SDL_Rect rect;
    rect.w = scale * 384 * pixel_w;
    rect.h = scale * 224 * pixel_h;
    center_rect(&rect, win_w, win_h);
    return rect;
}

static SDL_Rect get_letterbox_rect(int win_w, int win_h) {
    switch (scale_mode) {
    case SCALEMODE_NEAREST:
        return fit_4_by_3_rect(win_w, win_h);

    case SCALEMODE_INTEGER:
        // In order to scale a 384x224 buffer to 4:3 we need to stretch the image vertically by 9 / 7
        return fit_integer_rect(win_w, win_h, 7, 9);

    case SCALEMODE_SQUARE_PIXELS:
        return fit_integer_rect(win_w, win_h, 1, 1);

    default:
        return fit_4_by_3_rect(win_w, win_h);
    }
}

static void update_metrics(Uint64 sleep_time) {
    const Uint64 new_frame_end_time = SDL_GetTicksNS();
    const Uint64 frame_time = new_frame_end_time - last_frame_end_time;
    const float frame_time_ms = (float)frame_time / 1e6;

    frame_metrics.frame_time[frame_metrics.head] = frame_time_ms;
    frame_metrics.idle_time[frame_metrics.head] = (float)sleep_time / 1e6;
    frame_metrics.fps[frame_metrics.head] = 1000 / frame_time_ms;

    frame_metrics.head = (frame_metrics.head + 1) % SDL_arraysize(frame_metrics.frame_time);
    last_frame_end_time = new_frame_end_time;

#ifdef __vita__
    // Lightweight FPS trace: every 120 frames (~2s @ 60fps), log the rolling
    // average FPS + worst frame to console.log so the user can diagnose
    // stutter without UI tools. Pull console.log via USB to inspect.
    static int vita_fps_tick = 0;
    if (++vita_fps_tick >= 120) {
        vita_fps_tick = 0;
        float sum_fps = 0.0f;
        float max_frame = 0.0f;
        const int n = (int)SDL_arraysize(frame_metrics.fps);
        for (int i = 0; i < n; i++) {
            sum_fps += frame_metrics.fps[i];
            if (frame_metrics.frame_time[i] > max_frame) {
                max_frame = frame_metrics.frame_time[i];
            }
        }
        SDL_Log("FPS avg=%.1f worst_frame_ms=%.2f", sum_fps / (float)n, max_frame);
    }
#endif
}

#ifdef __ANDROID__
// Android's nanosleep / SDL_DelayNS has ~5-15ms granularity (no high-res
// timer like Windows). The desktop while(now<deadline) tight loop ends up
// overshooting by ~15ms, so a frame that finishes 1ms early sleeps until
// the *next* vsync — dropping effective FPS to ~20. Rely on the display's
// own vsync (Vulkan FIFO/MAILBOX present blocks) and skip the manual sleep.
static void android_pace_frame(Uint64* sleep_time_out) {
    // 120Hz phones blow past 60 even with FIFO vsync since FIFO blocks at
    // display refresh, not at our target_frame_time_ns. Explicit sleep keeps
    // the game logic at 60 fps regardless of panel refresh rate. We do still
    // want FIFO (or MAILBOX) so we don't waste GPU spinning between frames.
    Uint64 now = SDL_GetTicksNS();
    *sleep_time_out = 0;
    if (frame_deadline == 0) {
        frame_deadline = now + target_frame_time_ns;
        return;
    }
    if (now < frame_deadline) {
        *sleep_time_out = frame_deadline - now;
        SDL_DelayNS(*sleep_time_out);
        now = SDL_GetTicksNS();
    }
    frame_deadline += target_frame_time_ns;
    if (now > frame_deadline + target_frame_time_ns) {
        // Way behind (sustained jank). Drop the deadline so we don't burn
        // sleep budget chasing past frames.
        frame_deadline = now + target_frame_time_ns;
    }
}
#endif

static void end_frame() {
#if STATCHECK
    TestRunner_Epilogue();
#endif

    // Run sound processing
    ADX_ProcessTracks();

    // Render

#if NETPLAY_ENABLED
    // This should come before SDLGameRenderer_RenderFrame,
    // because NetstatsRenderer uses the existing SFIII rendering pipeline
    NetplayScreen_Render();
    NetstatsRenderer_Render();
#if defined(__vita__)
    VitaLogin_Render();
#endif
#endif

#if DEBUG
    // Render debug text
    // SDLDebugText_Render();
#endif

    int window_width;
    int window_height;
#ifdef __vita__
    // SDL3-Vita with the dummy driver doesn't know the real surface size,
    // so SDL_GetWindowSizeInPixels returns whatever we passed to SDL_CreateWindow
    // (the config defaults), not the physical 960x544 vitaGL is presenting to.
    // Hardcode the Vita panel to keep the letterbox calc honest.
    window_width = 960;
    window_height = 544;
#else
    SDL_GetWindowSizeInPixels(window, &window_width, &window_height);
#endif
    SDLGenericRenderer_RenderFrame(get_letterbox_rect(window_width, window_height));

    // Handle cursor hiding
    hide_cursor_if_needed();

    // Do frame pacing
    Uint64 sleep_time = 0;

#ifdef __ANDROID__
    // Android backends present-block via Vulkan FIFO/MAILBOX — no manual
    // sleep needed (and the desktop tight-loop kills effective FPS).
    android_pace_frame(&sleep_time);
#else
    Uint64 now = SDL_GetTicksNS();

    if (frame_deadline == 0) {
        frame_deadline = now + target_frame_time_ns;
    }

    // Loop the sleep — SDL_DelayNS on Windows can undershoot the requested
    // duration (timer slop, 15.6 ms default granularity unless high-res timer
    // is held). A single sleep call would let the frame run early and push
    // effective FPS well above the 60 cap on bursts, which is what users see
    // as "FPS unlocks" + the OS-wide stutter (full-throttle render loop).
    while (now < frame_deadline) {
        Uint64 remaining = frame_deadline - now;
        sleep_time += remaining;
        SDL_DelayNS(remaining);
        Uint64 new_now = SDL_GetTicksNS();
        if (new_now <= now) {
            // Defensive: clock didn't advance, bail to avoid infinite loop.
            break;
        }
        now = new_now;
    }

    frame_deadline += target_frame_time_ns;

    // If we fell behind by more than one frame, resync to avoid spiraling
    if (now > frame_deadline + target_frame_time_ns) {
        frame_deadline = now + target_frame_time_ns;
    }
#endif

    // Measure
    update_metrics(sleep_time);
}

// Entrypoint

static bool sdl_poll_helper() {
    SDL_Event event;
    bool continue_running = true;

    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            continue_running = false;
        }
    }

    return continue_running;
}

static int loop() {
    bool is_running = true;

    while (is_running) {
        switch (phase) {
        case APP_PHASE_INIT:
            SDL_Log("loop: APP_PHASE_INIT entering pre_init");
            pre_init();
            SDL_Log("loop: pre_init returned");

            if (Resources_Check()) {
                SDL_Log("loop: Resources_Check OK -> full_init");
                const int init_status = full_init();

                if (init_status != 0) {
                    SDL_Log("loop: full_init FAILED status=%d", init_status);
                    is_running = false;
                    break;
                }

                phase = APP_PHASE_INITIALIZED;
                SDL_Log("loop: phase -> INITIALIZED");
            } else {
                SDL_Log("loop: Resources_Check FAILED -> COPYING_RESOURCES");
                phase = APP_PHASE_COPYING_RESOURCES;
            }

            break;

        case APP_PHASE_COPYING_RESOURCES:
            is_running = sdl_poll_helper();

            if (!is_running) {
                break;
            }

            SDL_Delay(16);

            const bool resource_flow_ended = Resources_RunResourceCopyingFlow();

            if (resource_flow_ended) {
                const int init_status = full_init();

                if (init_status != 0) {
                    is_running = false;
                    break;
                }

                phase = APP_PHASE_INITIALIZED;
            }

            break;

        case APP_PHASE_INITIALIZED:
            is_running = poll_events();

            if (!is_running) {
                break;
            }

            begin_frame();
            Main_StepFrame();
#if NETPLAY_ENABLED
#if IMGUI
            LoginPanel_Tick();
#elif defined(__vita__)
            VitaLogin_Tick();
#endif
            Netplay_TickMatchmaking();
            Netplay_TickDirectP2P();
            Netplay_Run();
            Netplay_TickDiscord();
#endif
            end_frame();
            Main_FinishFrame();
            break;
        }
    }

    cleanup();
    SDL_Quit();
    return 0;
}

#ifdef __ANDROID__
// SDL_main.h on Android rewrites our main signature to the non-const form.
// Adapt by accepting char* argv[] and casting through to init_args, which
// expects const char* argv[]. Desktop keeps its const signature unchanged.
int main(int argc, char* argv[]) {
    // SDL_Init MUST run before any other SDL3 call on Android. SDL3's log
    // backend, GPU driver enumeration, and atomic ops all dereference an
    // internal subsystem pointer that is NULL until SDL_Init wires it up.
    // pre_init() inside loop() also calls SDL_Init(VIDEO) — that's safe;
    // SDL_Init is refcounted + idempotent.
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        // No SDL_Log here — log subsystem may not be live yet.
        __android_log_print(ANDROID_LOG_ERROR, "3sx",
                            "SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    // Verbose SDL logs so we can see backend selection / device creation
    // failures via adb logcat. Remove once Android builds are stable.
    SDL_SetLogPriorities(SDL_LOG_PRIORITY_VERBOSE);
    int n = SDL_GetNumGPUDrivers();
    SDL_Log("SDL_GPU drivers available: %d", n);
    for (int i = 0; i < n; i++) {
        SDL_Log("  [%d] %s", i, SDL_GetGPUDriver(i));
    }
    init_args(argc, (const char**)argv);
#if NETPLAY_ENABLED
    const Args* a = get_args();
    if (a->netplay.matchmaking_ip != NULL) {
        Netplay_SetMatchmakingParams(a->netplay.matchmaking_ip, a->netplay.matchmaking_port);
    }
    if (a->netplay.p2p_remote_ip != NULL) {
        Netplay_SetParams(a->netplay.p2p_local_player, a->netplay.p2p_remote_ip);
    }
#endif
    return loop();
}
#else
int main(int argc, const char* argv[]) {
    init_args(argc, argv);
#if NETPLAY_ENABLED
    const Args* a = get_args();
    if (a->netplay.matchmaking_ip != NULL) {
        Netplay_SetMatchmakingParams(a->netplay.matchmaking_ip, a->netplay.matchmaking_port);
    }
    if (a->netplay.p2p_remote_ip != NULL) {
        Netplay_SetParams(a->netplay.p2p_local_player, a->netplay.p2p_remote_ip);
    }
#endif
    return loop();
}
#endif

// Public API

const FrameMetrics* SDLApp_GetFrameMetrics() {
    return &frame_metrics;
}

void SDLApp_Exit() {
    SDL_Event quit_event;
    quit_event.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&quit_event);
}

#endif // CRS_APP_DRIVER_SDL
