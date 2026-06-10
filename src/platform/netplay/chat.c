#if NETPLAY_ENABLED

#include "platform/netplay/chat.h"
#include "platform/netplay/fistbump.h"

#define CHAT_HISTORY 64
static ChatMsg history[CHAT_SCOPE_COUNT][CHAT_HISTORY];
static int count[CHAT_SCOPE_COUNT];

static const char* scope_name(ChatScope s) {
    switch (s) {
        case CHAT_GENERAL: return "general";
        case CHAT_MATCH:   return "match";
        case CHAT_ROOM:    return "room";
        default:           return NULL;
    }
}

static ChatScope scope_from_name(const char* name) {
    if (SDL_strcmp(name, "general") == 0) return CHAT_GENERAL;
    if (SDL_strcmp(name, "match")   == 0) return CHAT_MATCH;
    if (SDL_strcmp(name, "room")    == 0) return CHAT_ROOM;
    return CHAT_SCOPE_COUNT;
}

void Chat_Init(void) {
    SDL_zeroa(history);
    SDL_zeroa(count);
}

void Chat_AppendIncoming(ChatScope scope, const char* author, const char* text) {
    if (scope < 0 || scope >= CHAT_SCOPE_COUNT) return;
    if (author == NULL || text == NULL) return;
    int slot = count[scope] % CHAT_HISTORY;
    ChatMsg* m = &history[scope][slot];
    SDL_strlcpy(m->author, author, sizeof(m->author));
    SDL_strlcpy(m->text, text, sizeof(m->text));
    m->ts_ms = SDL_GetTicks();
    count[scope]++;
}

bool Chat_Send(ChatScope scope, const char* text) {
    if (text == NULL || text[0] == '\0') return false;
    const char* sname = scope_name(scope);
    if (sname == NULL) return false;

    // Sanitize: drop \r \n from outgoing (they would split the protocol line)
    char clean[256];
    int j = 0;
    for (int i = 0; text[i] && j < (int)sizeof(clean) - 1; i++) {
        if (text[i] != '\r' && text[i] != '\n') {
            clean[j++] = text[i];
        }
    }
    clean[j] = '\0';

    char buf[320];
    int n = SDL_snprintf(buf, sizeof(buf), "CHAT %s %s\n", sname, clean);
    if (n <= 0 || n >= (int)sizeof(buf)) return false;
    return Fistbump_SendChatRaw(buf, (size_t)n);
}

const ChatMsg* Chat_GetHistory(ChatScope scope, int* out_count) {
    if (scope < 0 || scope >= CHAT_SCOPE_COUNT || out_count == NULL) {
        if (out_count) *out_count = 0;
        return NULL;
    }
    int n = count[scope];
    *out_count = n < CHAT_HISTORY ? n : CHAT_HISTORY;
    return history[scope];
}

void Chat_HandleIncomingLine(const char* scope_str, const char* author, const char* text) {
    ChatScope s = scope_from_name(scope_str);
    if (s == CHAT_SCOPE_COUNT) return;
    Chat_AppendIncoming(s, author, text);
}

#endif
