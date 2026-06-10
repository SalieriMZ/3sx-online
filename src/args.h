#ifndef ARGS_H
#define ARGS_H

#if NETPLAY_ENABLED
typedef struct NetplayArgs {
    int p2p_local_player;
    const char* p2p_remote_ip;
    const char* matchmaking_ip;
    int matchmaking_port;
    int force_relay;
} NetplayArgs;
#endif

#if STATCHECK
typedef struct StatcheckArgs {
    const char* states_path;
} StatcheckArgs;
#endif

typedef struct Args {
#if NETPLAY_ENABLED
    NetplayArgs netplay;
#endif
#if STATCHECK
    StatcheckArgs statcheck;
#endif
    char dummy; /// A dummy value to make compiler stop complaining about "excess elements in struct initializer"
} Args;

void init_args(int argc, const char* argv[]);
const Args* get_args();

#if NETPLAY_ENABLED
// Mutate the cached args.netplay.force_relay flag at runtime. Used by the
// in-game force-relay toggle so the next Fistbump_Start picks it up.
void Args_SetForceRelay(int v);
#endif

#if NETPLAY_ENABLED && (defined(__ANDROID__) || defined(__vita__))
// Mobile/handheld: apply default of force_relay=1 and pick up persisted
// prefs at boot. Desktop builds don't call this — Args_SetForceRelay alone
// suffices.
void Args_LoadNetplayPrefs(void);
#endif

#endif
