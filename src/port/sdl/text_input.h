#pragma once

// Cross-platform single-field text entry for the native online UI.
//
// One modal field open at a time. Backends:
//   - Vita    : sceImeDialog OSK (the system keyboard composites its own
//               full-screen overlay; the caller draws only a prompt).
//   - PC/Android (SDL3): SDL_StartTextInput + SDL_EVENT_TEXT_INPUT, with the
//               caller echoing TextInput_GetDisplay() via the SF3 sprite font.
//               On Android SDL raises the soft keyboard automatically.
//
// This replaces the ImGui InputText the desktop login overlay used, so the
// same native UI (promoted from the Vita login) can collect credentials on
// every platform without ImGui.

#include <stdbool.h>

typedef enum {
    TEXT_INPUT_PENDING,   // still editing
    TEXT_INPUT_DONE,      // user committed (Enter / IME accept)
    TEXT_INPUT_CANCELLED, // user cancelled (Esc / IME cancel / backed out)
} TextInputStatus;

// One-time init. On SDL backends pass the SDL_Window* (needed by
// SDL_StartTextInput); ignored on Vita. Safe to call again to update it.
void TextInput_Init(void* sdl_window);

// Open the field. title labels the Vita OSK; initial pre-fills the buffer;
// is_password masks the echo; max_len caps length (clamped to the internal
// buffer). Returns false if a field is already open or the backend failed.
bool TextInput_Open(const char* title, const char* initial, bool is_password, int max_len);

// Poll the open field. PENDING until the user commits/cancels; on DONE/
// CANCELLED writes the UTF-8 result (empty on cancel) into out and closes the
// field. Returns CANCELLED with "" when nothing is open.
TextInputStatus TextInput_Poll(char* out, int out_max);

bool TextInput_IsActive(void);

// Force-close the field; the next TextInput_Poll() returns CANCELLED.
void TextInput_Cancel(void);

// Commit the open field (next TextInput_Poll() returns DONE). For platforms with
// no Enter key (Android soft keyboard) — driven by an on-screen / pad OK button.
void TextInput_Commit(void);

// Re-raise the on-screen keyboard for the open field (Android soft keyboard);
// no-op on Vita (the sceImeDialog is a modal system dialog).
void TextInput_ReopenKeyboard(void);

// SDL backends: feed an SDL_Event* so the field can consume text/keys. Call
// from the SDL event loop while TextInput_IsActive(). No-op on Vita.
void TextInput_HandleSDLEvent(const void* sdl_event);

// The in-progress buffer, masked if password, for the caller to echo with
// SSPutStrPro on SDL backends. Returns "" when inactive or on Vita (the OSK
// shows its own field).
const char* TextInput_GetDisplay(void);
