// Tests the atomic file writing in src/pc/fs.c.
//
// The save file and config are rewritten in full every time they change, so
// a write interrupted partway through must never be able to destroy what
// was already on disk. These cases cover that directly, including the one
// that matters most: abandoning a write without committing it.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs.h"

static int failures;

#define CHECK(cond, what)                                                     \
    do {                                                                      \
        if (!(cond)) {                                                        \
            printf("  FAIL: %s\n", what);                                     \
            failures++;                                                       \
        } else {                                                              \
            printf("  ok:   %s\n", what);                                     \
        }                                                                     \
    } while (0)

static const char *path;
static char tmp_path[512];

// Reads the destination, returning the byte count (-1 if it is missing)
static int read_dest(char *buf, size_t size) {
    FILE *f = fopen(path, "rb");
    int n;

    if (f == NULL) {
        return -1;
    }
    n = (int) fread(buf, 1, size, f);
    fclose(f);
    return n;
}

static bool tmp_exists(void) {
    FILE *f = fopen(tmp_path, "rb");
    if (f != NULL) {
        fclose(f);
        return true;
    }
    return false;
}

int main(int argc, char **argv) {
    char buf[512], rd[512];
    FILE *f;

    path = argc > 1 ? argv[1] : "porttest_save.bin";
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    remove(path);
    remove(tmp_path);

    printf("fs atomic write\n");

    memset(buf, 0xAB, sizeof(buf));
    CHECK(fs_write_file_atomic(path, buf, sizeof(buf)), "creates a new file");
    CHECK(read_dest(rd, sizeof(rd)) == (int) sizeof(buf) && memcmp(rd, buf, sizeof(buf)) == 0,
          "contents match what was written");
    CHECK(!tmp_exists(), "no temporary file is left behind");

    memset(buf, 0xCD, sizeof(buf));
    CHECK(fs_write_file_atomic(path, buf, sizeof(buf)), "replaces an existing file");
    CHECK(read_dest(rd, sizeof(rd)) == (int) sizeof(buf) && rd[0] == (char) 0xCD
              && rd[sizeof(buf) - 1] == (char) 0xCD,
          "the replacement is complete, not partial");

    // Simulate being killed partway through: write to the stream, then walk
    // away without committing it
    f = fs_open_atomic(path);
    CHECK(f != NULL, "opens a stream for a formatted write");
    if (f != NULL) {
        fprintf(f, "half a file");
        fs_abort_atomic(f, path);
    }
    CHECK(read_dest(rd, sizeof(rd)) == (int) sizeof(buf) && rd[0] == (char) 0xCD,
          "an abandoned write leaves the original intact");
    CHECK(!tmp_exists(), "aborting a stream removes its temporary file");

    f = fs_open_atomic(path);
    if (f != NULL) {
        fprintf(f, "value %d\n", 42);
        CHECK(fs_close_atomic(f, path), "commits a formatted write");
    }
    memset(rd, 0, sizeof(rd));
    read_dest(rd, sizeof(rd) - 1);
    CHECK(strcmp(rd, "value 42\n") == 0, "formatted contents land intact");
    CHECK(!tmp_exists(), "no temporary file after committing");

    remove(path);
    remove(tmp_path);

    printf(failures == 0 ? "PASS\n" : "FAILED (%d)\n", failures);
    return failures != 0;
}
