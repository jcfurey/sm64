#include <stdio.h>
#include <string.h>

#include "fs.h"

#ifdef TARGET_IOS

#include <SDL2/SDL.h>

const char *fs_get_write_path(const char *filename) {
    static char path[1024];
    static char *pref_path = NULL;

    if (pref_path == NULL) {
        pref_path = SDL_GetPrefPath("sm64", "sm64-port");
        if (pref_path == NULL) {
            // Last resort: current directory (matches desktop behavior)
            return filename;
        }
    }
    snprintf(path, sizeof(path), "%s%s", pref_path, filename);
    return path;
}

#else

const char *fs_get_write_path(const char *filename) {
    return filename;
}

#endif
