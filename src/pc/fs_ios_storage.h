#ifndef FS_IOS_STORAGE_H
#define FS_IOS_STORAGE_H

#include <stdbool.h>
#include <stddef.h>

// Builds <home>/Documents/<filename>, creates Documents when necessary, and
// moves an existing file out of legacy_dir when the new location is empty.
// Returns false without changing dst when the new path cannot be used.
bool fs_ios_prepare_write_path(char *dst, size_t dst_size, const char *filename,
                               const char *home, const char *legacy_dir);

#endif
