#if IMGUI

#ifndef NETPLAY_PANEL_H
#define NETPLAY_PANEL_H

#include <stdbool.h>

void NetplayPanel_Init(void);
void NetplayPanel_Render(void);
void NetplayPanel_Show(void);
void NetplayPanel_Hide(void);
bool NetplayPanel_IsVisible(void);

#endif

#endif
