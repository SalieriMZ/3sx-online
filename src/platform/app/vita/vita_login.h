#pragma once

#if defined(__vita__) && NETPLAY_ENABLED

// Vita-native login + region picker. Renders via SSPutStrPro (no ImGui) and
// captures credentials via sceImeDialog. Drives fistbump auth when the
// state machine is in FISTBUMP_AWAITING_CREDENTIALS.

void VitaLogin_Init(void);
void VitaLogin_Tick(void);
void VitaLogin_Render(void);

#endif
