#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "fs_ios_storage.h"

static bool fs_format_path(char *dst, size_t dst_size, const char *directory,
                           const char *filename) {
    const char *separator;
    int length;

    if (dst == NULL || dst_size == 0 || directory == NULL || filename == NULL) {
        return false;
    }

    separator = directory[0] != '\0' && directory[strlen(directory) - 1] == '/' ? "" : "/";
    length = snprintf(dst, dst_size, "%s%s%s", directory, separator, filename);
    return length >= 0 && (size_t) length < dst_size;
}

static bool fs_ensure_directory(const char *path) {
    struct stat info;

    if (mkdir(path, 0700) == 0) {
        return true;
    }
    return errno == EEXIST && stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

bool fs_ios_prepare_write_path(char *dst, size_t dst_size, const char *filename,
                               const char *home, const char *legacy_dir) {
    char documents[1024];
    char legacy[1024];

    if (!fs_format_path(documents, sizeof(documents), home, "Documents") ||
        !fs_ensure_directory(documents) ||
        !fs_format_path(dst, dst_size, documents, filename)) {
        return false;
    }

    if (access(dst, F_OK) == 0) {
        return true;
    }
    if (errno != ENOENT) {
        return false;
    }

    if (legacy_dir == NULL ||
        !fs_format_path(legacy, sizeof(legacy), legacy_dir, filename)) {
        return true;
    }
    if (access(legacy, F_OK) != 0) {
        return errno == ENOENT;
    }

    // Both directories are inside one app container, so this is an atomic
    // same-volume move. On failure the caller continues using legacy_dir.
    return rename(legacy, dst) == 0;
}
