#ifndef PC_FS_H
#define PC_FS_H

// Resolves a bare filename to a path inside a per-user writable directory.
// On desktop platforms files are kept next to the executable as before, so
// the filename is returned unchanged. On iOS the app bundle is read-only and
// the process working directory is not writable, so files are placed in the
// app's sandboxed preferences directory (via SDL_GetPrefPath).
//
// The returned pointer refers to a static buffer that is overwritten by the
// next call.
const char *fs_get_write_path(const char *filename);

#endif
