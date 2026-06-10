#ifndef LOGIN_PANEL_H
#define LOGIN_PANEL_H

#include <stdbool.h>

void LoginPanel_Init(void);
void LoginPanel_Render(void);
void LoginPanel_Tick(void);          // drive Fistbump state machine per frame
bool LoginPanel_IsActive(void);
bool LoginPanel_HasPlayClicked(void);
// Mark the overlay visible — call when the user enters the Network menu so
// the login form pops up if needed.
void LoginPanel_Show(void);
void LoginPanel_Hide(void);
void LoginPanel_Toggle(void);
bool LoginPanel_IsVisible(void);

#endif
