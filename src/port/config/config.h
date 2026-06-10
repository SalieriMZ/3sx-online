#ifndef PORT_CONFIG_H
#define PORT_CONFIG_H

#include <stdbool.h>

#define CFG_KEY_FULLSCREEN "fullscreen"
#define CFG_KEY_WINDOW_WIDTH "window-width"
#define CFG_KEY_WINDOW_HEIGHT "window-height"
#define CFG_KEY_SCALEMODE "scale-mode"
#define CFG_KEY_SCANLINES "scanlines"
#define CFG_DRAW_PLAYERS_ABOVE_HUD "draw-players-above-hud"
#define CFG_ARCADE_BALANCE "arcade-balance"

/// Initialize config system
void Config_Init();

/// Destroy resources used by config system
void Config_Destroy();

/// Get the value associated with the given key as a `bool`
/// @return The value associated with `key` if `key` is among entries and the value's type is `bool`, `false` otherwise
bool Config_GetBool(const char* key);

/// Get the value associated with the given key as an `int`
/// @return The value associated with `key` if `key` is among entries and the value's type is `int`, `0` otherwise
int Config_GetInt(const char* key);

/// Get the value associated with the given key as a `string`
/// @return The value associated with `key` if `key` is among entries and the value's type is `string`, `NULL` otherwise
const char* Config_GetString(const char* key);

#define CFG_KEY_SHOW_HITBOXES "show-hitboxes"
#define CFG_KEY_SHOW_FPS "show-fps"
#define CFG_KEY_HUD_SCALE_PCT "hud-scale-pct"
#define CFG_KEY_DISCORD_RPC "discord-rpc"
#define CFG_KEY_NETPLAY_REGION_IDX "netplay-region-idx"
#define CFG_KEY_NETPLAY_FORCE_RELAY "netplay-force-relay"
#define CFG_KEY_NETPLAY_USERNAME "netplay-username"
#define CFG_KEY_NETPLAY_REMEMBER_PW "netplay-remember-pw"

bool Config_SetBool(const char* key, bool value);
bool Config_SetInt(const char* key, int value);
bool Config_SetString(const char* key, const char* value);

#endif
