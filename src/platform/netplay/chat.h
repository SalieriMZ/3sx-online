#if NETPLAY_ENABLED

#ifndef NETPLAY_CHAT_H
#define NETPLAY_CHAT_H

#include <SDL3/SDL.h>
#include <stdbool.h>

typedef enum {
    CHAT_GENERAL = 0,
    CHAT_MATCH = 1,
    CHAT_ROOM = 2,
    CHAT_SCOPE_COUNT = 3
} ChatScope;

typedef struct {
    char author[32];
    char text[256];
    Uint64 ts_ms;
} ChatMsg;

void Chat_Init(void);
void Chat_AppendIncoming(ChatScope scope, const char* author, const char* text);
bool Chat_Send(ChatScope scope, const char* text);
const ChatMsg* Chat_GetHistory(ChatScope scope, int* out_count);
void Chat_HandleIncomingLine(const char* scope_str, const char* author, const char* text);

#endif

#endif
