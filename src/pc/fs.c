#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs.h"

#if defined(_WIN32) || defined(_WIN64)
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#include <fcntl.h>
#endif

#ifdef TARGET_IOS

#include <SDL2/SDL.h>
#include "fs_ios_storage.h"

const char *fs_get_write_path(const char *filename) {
    static char path[1024];
    static char *legacy_path = NULL;

    if (legacy_path == NULL) {
        legacy_path = SDL_GetPrefPath("sm64", "sm64-port");
    }

    // File sharing only exposes Documents. Move files from SDL's historical
    // Library/Application Support location the first time each one is used.
    if (fs_ios_prepare_write_path(path, sizeof(path), filename, getenv("HOME"), legacy_path)) {
        return path;
    }

    // If Documents is unavailable, preserve the old behavior rather than
    // making an existing save appear to vanish.
    if (legacy_path != NULL) {
        int length = snprintf(path, sizeof(path), "%s%s", legacy_path, filename);
        if (length >= 0 && (size_t) length < sizeof(path)) {
            return path;
        }
    }

    // Last resort: current directory (matches desktop behavior).
    return filename;
}

#else

const char *fs_get_write_path(const char *filename) {
    return filename;
}

#endif

//------------------------------------------------------------------------------
// Atomic file writing
//
// Opening a save file with "w" truncates it before a single byte is written,
// so an interruption in that window (which on mobile means an ordinary
// out-of-memory kill) leaves an empty file where the save used to be. These
// helpers write to a sibling temporary file, flush it all the way to storage,
// and only then rename it over the destination. A rename either happens or it
// does not, so the destination always holds either the old contents or the
// complete new ones.
//------------------------------------------------------------------------------

#define FS_TMP_SUFFIX ".tmp"

// Builds "<path><FS_TMP_SUFFIX>", returning false if it would not fit
static bool fs_build_tmp_path(char *dst, size_t dst_size, const char *path) {
    size_t len = strlen(path);
    if (len + sizeof(FS_TMP_SUFFIX) > dst_size) {
        return false;
    }
    memcpy(dst, path, len);
    memcpy(dst + len, FS_TMP_SUFFIX, sizeof(FS_TMP_SUFFIX));
    return true;
}

// Flushes a stream's contents out of both the C library and the OS page cache
static bool fs_sync_stream(FILE *file) {
    if (fflush(file) != 0) {
        return false;
    }
#if defined(_WIN32) || defined(_WIN64)
    return _commit(_fileno(file)) == 0;
#else
    // EINVAL means the target does not support syncing (pipes, some virtual
    // filesystems); the data is still out of our hands, so accept it
    if (fsync(fileno(file)) != 0) {
        return errno == EINVAL;
    }
    return true;
#endif
}

// Syncs the directory containing 'path' so the rename itself is durable.
// Without this a crash can leave the rename unrecorded even though the file
// contents were synced. Windows has no equivalent and does not need one.
static void fs_sync_parent_dir(const char *path) {
#if defined(_WIN32) || defined(_WIN64)
    (void) path;
#else
    char dir[1024];
    const char *slash = strrchr(path, '/');
    size_t len;
    int fd;

    if (slash == NULL) {
        dir[0] = '.';
        dir[1] = '\0';
    } else {
        len = (size_t) (slash - path);
        if (len == 0) {
            len = 1; // the root directory itself
        }
        if (len >= sizeof(dir)) {
            return;
        }
        memcpy(dir, path, len);
        dir[len] = '\0';
    }

    fd = open(dir, O_RDONLY);
    if (fd >= 0) {
        fsync(fd);
        close(fd);
    }
#endif
}

// Moves the completed temporary file over the destination
static bool fs_commit_tmp(const char *tmp_path, const char *path) {
#if defined(_WIN32) || defined(_WIN64)
    if (!MoveFileExA(tmp_path, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        remove(tmp_path);
        return false;
    }
#else
    if (rename(tmp_path, path) != 0) {
        remove(tmp_path);
        return false;
    }
#endif
    fs_sync_parent_dir(path);
    return true;
}

FILE *fs_open_atomic(const char *path) {
    char tmp_path[1024];

    if (!fs_build_tmp_path(tmp_path, sizeof(tmp_path), path)) {
        return NULL;
    }
    return fopen(tmp_path, "wb");
}

bool fs_close_atomic(FILE *file, const char *path) {
    char tmp_path[1024];
    bool ok;

    if (file == NULL) {
        return false;
    }
    if (!fs_build_tmp_path(tmp_path, sizeof(tmp_path), path)) {
        fclose(file);
        return false;
    }

    ok = fs_sync_stream(file);
    if (fclose(file) != 0) {
        ok = false;
    }
    if (!ok) {
        remove(tmp_path);
        return false;
    }
    return fs_commit_tmp(tmp_path, path);
}

void fs_abort_atomic(FILE *file, const char *path) {
    char tmp_path[1024];

    if (file != NULL) {
        fclose(file);
    }
    if (fs_build_tmp_path(tmp_path, sizeof(tmp_path), path)) {
        remove(tmp_path);
    }
}

bool fs_write_file_atomic(const char *path, const void *data, size_t size) {
    FILE *file = fs_open_atomic(path);

    if (file == NULL) {
        return false;
    }
    if (size != 0 && fwrite(data, 1, size, file) != size) {
        fs_abort_atomic(file, path);
        return false;
    }
    return fs_close_atomic(file, path);
}
