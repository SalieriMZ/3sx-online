#include "port/paths.h"

#include <SDL3/SDL.h>

static const char* pref_path = NULL;

const char* Paths_GetPrefPath() {
    if (pref_path == NULL) {
        pref_path = SDL_GetPrefPath("CrowdedStreet", "3SX");
    }

    return pref_path;
}

const char* Paths_GetBasePath() {
#ifdef __ANDROID__
    // Android apps have no traditional filesystem base. We stage assets
    // (shaders, ROM) into the per-app filesDir at install time via the
    // companion install-and-run.bat script. Return that path so the
    // generic asset loader finds them.
    return Paths_GetPrefPath();
#else
    return SDL_GetBasePath();
#endif
}
