#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "pc/fs_ios_storage.h"

static void make_dir(const char *path) {
    assert(mkdir(path, 0700) == 0 || errno == EEXIST);
}

static void write_text(const char *path, const char *text) {
    FILE *file = fopen(path, "wb");
    assert(file != NULL);
    assert(fwrite(text, 1, strlen(text), file) == strlen(text));
    assert(fclose(file) == 0);
}

static void expect_text(const char *path, const char *expected) {
    char contents[64] = { 0 };
    FILE *file = fopen(path, "rb");
    assert(file != NULL);
    assert(fread(contents, 1, sizeof(contents) - 1, file) == strlen(expected));
    assert(fclose(file) == 0);
    assert(strcmp(contents, expected) == 0);
}

int main(void) {
    char root[] = "/tmp/sm64-fs-ios-XXXXXX";
    char library[1024];
    char app_support[1024];
    char legacy_dir[1024];
    char legacy_save[1024];
    char documents[1024];
    char new_save[1024];
    char path[1024];
    char tiny[8];
    struct stat info;

    assert(mkdtemp(root) != NULL);
    snprintf(library, sizeof(library), "%s/Library", root);
    snprintf(app_support, sizeof(app_support), "%s/Application Support", library);
    snprintf(legacy_dir, sizeof(legacy_dir), "%s/sm64-port", app_support);
    snprintf(legacy_save, sizeof(legacy_save), "%s/sm64_save_file.bin", legacy_dir);
    snprintf(documents, sizeof(documents), "%s/Documents", root);
    snprintf(new_save, sizeof(new_save), "%s/sm64_save_file.bin", documents);

    make_dir(library);
    make_dir(app_support);
    make_dir(legacy_dir);
    write_text(legacy_save, "legacy-save");

    assert(fs_ios_prepare_write_path(path, sizeof(path), "sm64_save_file.bin",
                                     root, legacy_dir));
    assert(strcmp(path, new_save) == 0);
    assert(stat(documents, &info) == 0 && S_ISDIR(info.st_mode));
    assert(access(legacy_save, F_OK) != 0 && errno == ENOENT);
    expect_text(new_save, "legacy-save");

    // A file already in Documents wins; a stale legacy copy is not allowed
    // to overwrite it.
    snprintf(legacy_save, sizeof(legacy_save), "%s/sm64config.txt", legacy_dir);
    snprintf(new_save, sizeof(new_save), "%s/sm64config.txt", documents);
    write_text(legacy_save, "old-config");
    write_text(new_save, "new-config");
    assert(fs_ios_prepare_write_path(path, sizeof(path), "sm64config.txt",
                                     root, legacy_dir));
    expect_text(new_save, "new-config");
    expect_text(legacy_save, "old-config");

    assert(!fs_ios_prepare_write_path(tiny, sizeof(tiny), "too-long.bin",
                                      root, legacy_dir));
    assert(!fs_ios_prepare_write_path(path, sizeof(path), "save.bin",
                                      NULL, legacy_dir));

    unlink(legacy_save);
    unlink(new_save);
    snprintf(new_save, sizeof(new_save), "%s/sm64_save_file.bin", documents);
    unlink(new_save);
    rmdir(documents);
    rmdir(legacy_dir);
    rmdir(app_support);
    rmdir(library);
    rmdir(root);

    puts("iOS storage: 4/4 checks passed");
    return 0;
}
