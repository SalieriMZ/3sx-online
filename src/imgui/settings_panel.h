#if IMGUI

#ifndef SETTINGS_PANEL_H
#define SETTINGS_PANEL_H

#include <stdbool.h>

void SettingsPanel_Init(void);
void SettingsPanel_Render(void);
void SettingsPanel_RenderFpsOverlay(void);
void SettingsPanel_Show(void);
void SettingsPanel_Hide(void);
void SettingsPanel_Toggle(void);
bool SettingsPanel_IsVisible(void);

#endif

#endif
