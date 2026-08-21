#ifndef PC_FS_H
#define PC_FS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

// Resolves a bare filename to a path inside a per-user writable directory.
// On desktop platforms files are kept next to the executable as before, so
// the filename is returned unchanged. On iOS the app bundle is read-only and
// the process working directory is not writable, so files are placed in the
// app's sandboxed preferences directory (via SDL_GetPrefPath).
//
// The returned pointer refers to a static buffer that is overwritten by the
// next call.
const char *fs_get_write_path(const char *filename);

// Writes 'size' bytes to 'path' so that the file is either fully replaced or
// left untouched, never truncated halfway. Returns false if the destination
// still holds its previous contents.
bool fs_write_file_atomic(const char *path, const void *data, size_t size);

// The same guarantee for callers that need to write through a FILE * (for
// formatted output). Write to the returned stream, then hand it to
// fs_close_atomic, which closes it and puts it in place; on failure the
// destination is left untouched. Do not fclose the stream yourself.
FILE *fs_open_atomic(const char *path);
bool fs_close_atomic(FILE *file, const char *path);

#endif
