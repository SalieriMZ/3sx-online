#include "port/sdl/text_input.h"

#include <SDL3/SDL.h>
#include <string.h>

#define TI_BUF_MAX 256

// Masked echo + raw buffer shared by both backends.
static char ti_buf[TI_BUF_MAX];
static char ti_display[TI_BUF_MAX];
static bool ti_active = false;
static bool ti_password = false;
static int  ti_max_len = TI_BUF_MAX - 1;

static void ti_rebuild_display(void) {
    int n = (int)strlen(ti_buf);
    if (n > TI_BUF_MAX - 1) {
        n = TI_BUF_MAX - 1;
    }
    if (ti_password) {
        for (int i = 0; i < n; i++) {
            ti_display[i] = '*';
        }
        ti_display[n] = '\0';
    } else {
        memcpy(ti_display, ti_buf, (size_t)n);
        ti_display[n] = '\0';
    }
}

static void ti_reset(void) {
    ti_active = false;
    ti_buf[0] = '\0';
    ti_display[0] = '\0';
}

bool TextInput_IsActive(void) {
    return ti_active;
}

const char* TextInput_GetDisplay(void) {
    return ti_active ? ti_display : "";
}

// ===========================================================================
//  Vita backend — sceImeDialog
// ===========================================================================
#if defined(__vita__)

#include <psp2/common_dialog.h>
#include <psp2/ime_dialog.h>
#include <psp2/sysmodule.h>

// Gates vglSwapBuffers compositing of the Sce system dialog overlay
// (opengl_renderer.c externs this). Owned here now that the IME lives in the
// shared text-input backend rather than the old vita_login module.
bool vita_common_dialog_active = false;

static bool ti_ime_module_loaded = false;
static SceWChar16 ti_ime_title[64];
static SceWChar16 ti_ime_buf[TI_BUF_MAX];
static TextInputStatus ti_status = TEXT_INPUT_PENDING;

static void ti_utf8_to_utf16(const char* in, SceWChar16* out, size_t out_max) {
    size_t i = 0;
    for (; in[i] != '\0' && i + 1 < out_max; i++) {
        out[i] = (SceWChar16)(unsigned char)in[i];
    }
    out[i] = 0;
}

static void ti_utf16_to_utf8(const SceWChar16* in, char* out, size_t out_max) {
    size_t i = 0;
    for (; in[i] != 0 && i + 1 < out_max; i++) {
        // BASIC_LATIN dialog only emits 0x20-0x7E; truncate the high byte.
        out[i] = (char)(in[i] & 0x7F);
    }
    out[i] = '\0';
}

static bool ti_ensure_module(void) {
    if (ti_ime_module_loaded) {
        return true;
    }
    if (sceSysmoduleLoadModule(SCE_SYSMODULE_IME) < 0) {
        return false;
    }
    ti_ime_module_loaded = true;
    return true;
}

void TextInput_Init(void* sdl_window) {
    (void)sdl_window;
}

bool TextInput_Open(const char* title, const char* initial, bool is_password, int max_len) {
    if (ti_active) {
        return false;
    }
    if (!ti_ensure_module()) {
        return false;
    }
    if (max_len < 1 || max_len > TI_BUF_MAX - 1) {
        max_len = TI_BUF_MAX - 1;
    }
    ti_password = is_password;
    ti_max_len = max_len;
    ti_status = TEXT_INPUT_PENDING;

    ti_utf8_to_utf16(title != NULL ? title : "", ti_ime_title,
                     sizeof(ti_ime_title) / sizeof(ti_ime_title[0]));

    SceImeDialogParam param;
    sceImeDialogParamInit(&param);
    param.supportedLanguages = 0;
    param.languagesForced = SCE_FALSE;
    param.type = SCE_IME_TYPE_BASIC_LATIN;
    param.option = is_password ? SCE_IME_OPTION_NO_AUTO_CAPITALIZATION : 0;
    param.title = ti_ime_title;
    param.maxTextLength = (SceUInt32)max_len;
    param.inputTextBuffer = ti_ime_buf;
    static SceWChar16 ti_init_buf[TI_BUF_MAX];
    if (initial != NULL && initial[0] != '\0') {
        ti_utf8_to_utf16(initial, ti_init_buf, sizeof(ti_init_buf) / sizeof(ti_init_buf[0]));
        param.initialText = ti_init_buf;
    }
    SDL_zeroa(ti_ime_buf);

    if (sceImeDialogInit(&param) < 0) {
        return false;
    }
    vita_common_dialog_active = true;
    ti_active = true;
    ti_buf[0] = '\0';
    ti_display[0] = '\0'; // OSK shows its own field; no inline echo on Vita.
    return true;
}

TextInputStatus TextInput_Poll(char* out, int out_max) {
    if (!ti_active) {
        if (out != NULL && out_max > 0) {
            out[0] = '\0';
        }
        return TEXT_INPUT_CANCELLED;
    }
    if (ti_status == TEXT_INPUT_PENDING) {
        if (sceImeDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED) {
            return TEXT_INPUT_PENDING;
        }
        SceImeDialogResult res;
        SDL_zero(res);
        sceImeDialogGetResult(&res);
        sceImeDialogTerm();
        vita_common_dialog_active = false;
        if (res.button == SCE_IME_DIALOG_BUTTON_ENTER) {
            ti_utf16_to_utf8(ti_ime_buf, ti_buf, sizeof(ti_buf));
            ti_status = TEXT_INPUT_DONE;
        } else {
            ti_buf[0] = '\0';
            ti_status = TEXT_INPUT_CANCELLED;
        }
    }
    if (out != NULL && out_max > 0) {
        if (ti_status == TEXT_INPUT_DONE) {
            SDL_strlcpy(out, ti_buf, (size_t)out_max);
        } else {
            out[0] = '\0';
        }
    }
    TextInputStatus s = ti_status;
    ti_reset();
    return s;
}

void TextInput_Cancel(void) {
    if (!ti_active) {
        return;
    }
    sceImeDialogTerm();
    vita_common_dialog_active = false;
    ti_status = TEXT_INPUT_CANCELLED;
    ti_buf[0] = '\0';
}

void TextInput_Commit(void) {
    // The sceImeDialog supplies its own accept button; nothing to force.
}

void TextInput_ReopenKeyboard(void) {
    // sceImeDialog is a modal system dialog; can't be re-raised externally.
}

void TextInput_HandleSDLEvent(const void* sdl_event) {
    (void)sdl_event; // sceIme polls its own status.
}

// ===========================================================================
//  SDL3 backend — PC / Android
// ===========================================================================
#else

static SDL_Window* ti_window = NULL;
static TextInputStatus ti_status = TEXT_INPUT_PENDING;
static bool ti_raise_pending = false; // raise the soft keyboard on the next Poll

void TextInput_Init(void* sdl_window) {
    ti_window = (SDL_Window*)sdl_window;
}

bool TextInput_Open(const char* title, const char* initial, bool is_password, int max_len) {
    (void)title; // No system title bar on the SDL path; the caller draws a prompt.
    if (ti_active) {
        return false;
    }
    if (max_len < 1 || max_len > TI_BUF_MAX - 1) {
        max_len = TI_BUF_MAX - 1;
    }
    ti_password = is_password;
    ti_max_len = max_len;
    ti_status = TEXT_INPUT_PENDING;

    ti_buf[0] = '\0';
    if (initial != NULL && initial[0] != '\0') {
        SDL_strlcpy(ti_buf, initial, (size_t)(max_len + 1));
    }
    ti_active = true;
    ti_rebuild_display();

    // Defer raising the soft keyboard to the next Poll. Raising it the same frame
    // a previous field was stopped (Android field-to-field chain) gets dropped by
    // the IME — a one-frame gap makes the re-show reliable.
    ti_raise_pending = true;
    return true;
}

void TextInput_HandleSDLEvent(const void* sdl_event) {
    // Once Enter/Esc has latched a result, ignore any trailing text/key events
    // in the same poll batch so a near-simultaneous keystroke can't append to
    // the already-committed value.
    if (!ti_active || ti_status != TEXT_INPUT_PENDING || sdl_event == NULL) {
        return;
    }
    const SDL_Event* e = (const SDL_Event*)sdl_event;

    if (e->type == SDL_EVENT_TEXT_INPUT) {
        const char* in = e->text.text;
        int len = (int)strlen(ti_buf);
        for (int i = 0; in[i] != '\0' && len < ti_max_len; i++) {
            // Keep it to single-byte printable ASCII — usernames/passwords are
            // ASCII and the wire protocol is space-delimited, so reject control
            // chars and spaces defensively.
            unsigned char c = (unsigned char)in[i];
            if (c < 0x21 || c > 0x7E) {
                continue;
            }
            ti_buf[len++] = (char)c;
        }
        ti_buf[len] = '\0';
        ti_rebuild_display();
        return;
    }

    if (e->type == SDL_EVENT_KEY_DOWN) {
        switch (e->key.key) {
        case SDLK_BACKSPACE: {
            int len = (int)strlen(ti_buf);
            if (len > 0) {
                ti_buf[len - 1] = '\0';
                ti_rebuild_display();
            }
            break;
        }
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            ti_status = TEXT_INPUT_DONE;
            break;
        case SDLK_ESCAPE:
            ti_status = TEXT_INPUT_CANCELLED;
            break;
        default:
            break;
        }
    }
}

TextInputStatus TextInput_Poll(char* out, int out_max) {
    if (!ti_active) {
        if (out != NULL && out_max > 0) {
            out[0] = '\0';
        }
        return TEXT_INPUT_CANCELLED;
    }
    if (ti_status == TEXT_INPUT_PENDING) {
        // Deferred raise (one frame after Open / ReopenKeyboard) so the IME
        // reliably re-shows on Android field-to-field chains.
        if (ti_raise_pending && ti_window != NULL) {
            SDL_StartTextInput(ti_window);
        }
        ti_raise_pending = false;
        return TEXT_INPUT_PENDING;
    }
    if (out != NULL && out_max > 0) {
        if (ti_status == TEXT_INPUT_DONE) {
            SDL_strlcpy(out, ti_buf, (size_t)out_max);
        } else {
            out[0] = '\0';
        }
    }
    if (ti_window != NULL) {
        SDL_StopTextInput(ti_window);
    }
    ti_raise_pending = false;
    TextInputStatus s = ti_status;
    ti_reset();
    return s;
}

void TextInput_Cancel(void) {
    if (!ti_active) {
        return;
    }
    ti_status = TEXT_INPUT_CANCELLED;
}

void TextInput_Commit(void) {
    if (ti_active && ti_status == TEXT_INPUT_PENDING) {
        ti_status = TEXT_INPUT_DONE;
    }
}

void TextInput_ReopenKeyboard(void) {
    // Re-raise the soft keyboard (Android) for the still-open field: stop now,
    // re-start on the next Poll (one-frame gap so the IME honors the re-show).
    if (ti_active && ti_window != NULL) {
        SDL_StopTextInput(ti_window);
        ti_raise_pending = true;
    }
}

#endif
