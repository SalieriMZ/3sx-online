#ifndef PLATFORM_NETPLAY_DISCORD_RPC_H
#define PLATFORM_NETPLAY_DISCORD_RPC_H

#include <stdbool.h>

// Minimal Discord Rich Presence client over the local IPC named pipe.
// Pass an empty app_id to skip integration; otherwise register your own app
// at https://discord.com/developers/applications and pass its ID here.

void DiscordRPC_Init(const char* app_id);
void DiscordRPC_Shutdown(void);
void DiscordRPC_SetActivity(const char* state, const char* details,
                            const char* image_key, long long start_ts);
void DiscordRPC_ClearActivity(void);
void DiscordRPC_SetEnabled(bool enabled);
bool DiscordRPC_IsEnabled(void);

#endif
