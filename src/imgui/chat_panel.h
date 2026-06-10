#if IMGUI

#ifndef CHAT_PANEL_H
#define CHAT_PANEL_H

#include <stdbool.h>

void ChatPanel_Init(void);
void ChatPanel_Render(void);
void ChatPanel_Toggle(void);
bool ChatPanel_IsVisible(void);

#endif

#endif
