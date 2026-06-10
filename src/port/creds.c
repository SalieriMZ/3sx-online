#include "port/creds.h"
#include "port/paths.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>

static void cred_path(char* out, size_t out_size) {
    SDL_snprintf(out, out_size, "%s/cred", Paths_GetPrefPath());
}

bool Creds_SavePassword(const char* pw) {
    if (pw == NULL || pw[0] == '\0') {
        return false;
    }

    DATA_BLOB in;
    in.cbData = (DWORD)strlen(pw);
    in.pbData = (BYTE*)pw;

    DATA_BLOB out = { 0 };
    if (!CryptProtectData(&in, L"3sx-password", NULL, NULL, NULL, 0, &out)) {
        return false;
    }

    char path[512];
    cred_path(path, sizeof(path));

    SDL_IOStream* s = SDL_IOFromFile(path, "wb");
    bool ok = false;
    if (s != NULL) {
        size_t written = SDL_WriteIO(s, out.pbData, out.cbData);
        SDL_CloseIO(s);
        ok = (written == out.cbData);
    }

    LocalFree(out.pbData);
    return ok;
}

bool Creds_LoadPassword(char* buf, size_t buf_size) {
    if (buf == NULL || buf_size == 0) {
        return false;
    }
    buf[0] = '\0';

    char path[512];
    cred_path(path, sizeof(path));

    SDL_IOStream* s = SDL_IOFromFile(path, "rb");
    if (s == NULL) {
        return false;
    }

    Sint64 size = SDL_GetIOSize(s);
    if (size <= 0 || size > 4096) {
        SDL_CloseIO(s);
        return false;
    }

    BYTE* enc = (BYTE*)SDL_malloc((size_t)size);
    if (enc == NULL) {
        SDL_CloseIO(s);
        return false;
    }
    size_t read = SDL_ReadIO(s, enc, (size_t)size);
    SDL_CloseIO(s);
    if (read != (size_t)size) {
        SDL_free(enc);
        return false;
    }

    DATA_BLOB in;
    in.cbData = (DWORD)size;
    in.pbData = enc;

    DATA_BLOB out = { 0 };
    BOOL ok = CryptUnprotectData(&in, NULL, NULL, NULL, NULL, 0, &out);
    SDL_free(enc);
    if (!ok) {
        return false;
    }

    size_t copy = (out.cbData < buf_size - 1) ? out.cbData : buf_size - 1;
    memcpy(buf, out.pbData, copy);
    buf[copy] = '\0';

    LocalFree(out.pbData);
    return true;
}

void Creds_ClearPassword(void) {
    char path[512];
    cred_path(path, sizeof(path));
    remove(path);
}

#elif defined(__vita__)

// Vita has no DPAPI. No on-screen keyboard wired yet either, so the user
// stages a refresh-token file from PC over USB at
// ux0:data/CrowdedStreet/3SX/cred (plaintext, one line, no trailing newline).
// Save is a no-op — there's no UI to type a password and rewriting the file
// from inside the app would lose the user's manually-pasted token.

static void cred_path(char* out, size_t out_size) {
    SDL_snprintf(out, out_size, "%scred", Paths_GetPrefPath());
}

bool Creds_SavePassword(const char* pw) {
    (void)pw;
    return false;
}

bool Creds_LoadPassword(char* buf, size_t buf_size) {
    if (buf == NULL || buf_size == 0) {
        return false;
    }
    buf[0] = '\0';

    char path[512];
    cred_path(path, sizeof(path));

    SDL_IOStream* s = SDL_IOFromFile(path, "rb");
    if (s == NULL) {
        return false;
    }

    Sint64 size = SDL_GetIOSize(s);
    if (size <= 0 || (Sint64)size >= (Sint64)buf_size) {
        SDL_CloseIO(s);
        return false;
    }

    size_t read = SDL_ReadIO(s, buf, (size_t)size);
    SDL_CloseIO(s);
    if (read == 0) {
        buf[0] = '\0';
        return false;
    }

    buf[read] = '\0';
    // Trim trailing whitespace / newlines.
    while (read > 0 && (buf[read - 1] == '\n' || buf[read - 1] == '\r' || buf[read - 1] == ' ')) {
        buf[--read] = '\0';
    }
    return read > 0;
}

void Creds_ClearPassword(void) {
    // Keep the user's staged file; never delete it from inside the app.
}

#elif defined(__ANDROID__)

// Android: app data dir is sandboxed per-app (private internal storage). Only
// the app's UID can read its own files. That's strong enough for a netplay
// password — no extra encryption layer needed. Plain JSON in the prefpath.
//
// Format: {"pw":"<password>"}
//   - Single-line JSON for easy parsing.
//   - No newline / no whitespace.
//   - Empty file == no saved password.

static void cred_path(char* out, size_t out_size) {
    SDL_snprintf(out, out_size, "%s/cred.json", Paths_GetPrefPath());
}

static bool escape_into(const char* in, char* out, size_t out_size) {
    size_t oi = 0;
    for (size_t i = 0; in[i] != '\0'; i++) {
        char c = in[i];
        if (oi + 2 >= out_size) return false;
        if (c == '"' || c == '\\') {
            out[oi++] = '\\';
        }
        out[oi++] = c;
    }
    out[oi] = '\0';
    return true;
}

bool Creds_SavePassword(const char* pw) {
    if (pw == NULL || pw[0] == '\0') {
        return false;
    }
    char esc[256];
    if (!escape_into(pw, esc, sizeof(esc))) {
        return false;
    }
    char body[320];
    int n = SDL_snprintf(body, sizeof(body), "{\"pw\":\"%s\"}", esc);
    if (n < 0 || n >= (int)sizeof(body)) {
        return false;
    }

    char path[512];
    cred_path(path, sizeof(path));

    SDL_IOStream* s = SDL_IOFromFile(path, "wb");
    if (s == NULL) {
        return false;
    }
    size_t written = SDL_WriteIO(s, body, (size_t)n);
    SDL_CloseIO(s);
    return written == (size_t)n;
}

bool Creds_LoadPassword(char* buf, size_t buf_size) {
    if (buf == NULL || buf_size == 0) {
        return false;
    }
    buf[0] = '\0';

    char path[512];
    cred_path(path, sizeof(path));

    SDL_IOStream* s = SDL_IOFromFile(path, "rb");
    if (s == NULL) {
        return false;
    }
    char body[320];
    Sint64 size = SDL_GetIOSize(s);
    if (size <= 0 || size >= (Sint64)sizeof(body)) {
        SDL_CloseIO(s);
        return false;
    }
    size_t read = SDL_ReadIO(s, body, (size_t)size);
    SDL_CloseIO(s);
    if (read != (size_t)size) {
        return false;
    }
    body[size] = '\0';

    const char* p = SDL_strstr(body, "\"pw\":\"");
    if (p == NULL) {
        return false;
    }
    p += 6;
    size_t oi = 0;
    while (*p != '\0' && oi + 1 < buf_size) {
        if (*p == '\\' && p[1] != '\0') {
            buf[oi++] = p[1];
            p += 2;
        } else if (*p == '"') {
            break;
        } else {
            buf[oi++] = *p++;
        }
    }
    buf[oi] = '\0';
    return oi > 0;
}

void Creds_ClearPassword(void) {
    char path[512];
    cred_path(path, sizeof(path));
    remove(path);
}

#else  // !_WIN32 && !__vita__ && !__ANDROID__ — Linux/macOS NYI

bool Creds_SavePassword(const char* pw) {
    (void)pw;
    return false;
}

bool Creds_LoadPassword(char* buf, size_t buf_size) {
    if (buf != NULL && buf_size > 0) buf[0] = '\0';
    (void)buf_size;
    return false;
}

void Creds_ClearPassword(void) {}

#endif
