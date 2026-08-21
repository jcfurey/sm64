#include <stdio.h>
#include <ultra64.h>

#include "controller_api.h"

static FILE *fp;

static void tas_init(void) {
    fp = fopen("cont.m64", "rb");
    if (fp != NULL) {
        // Skip the .m64 header; a file too short to hold one has no inputs
        uint8_t buf[0x400];
        if (fread(buf, 1, sizeof(buf), fp) != sizeof(buf)) {
            fclose(fp);
            fp = NULL;
        }
    }
}

static void tas_read(OSContPad *pad) {
    if (fp != NULL) {
        uint8_t bytes[4] = {0};
        // At the end of the recording, leave the controller neutral rather
        // than replaying whatever was last in the buffer
        if (fread(bytes, 1, 4, fp) != 4) {
            fclose(fp);
            fp = NULL;
            return; // a partial record is not input
        }
        pad->button = (bytes[0] << 8) | bytes[1];
        pad->stick_x = bytes[2];
        pad->stick_y = bytes[3];
    }
}

struct ControllerAPI controller_recorded_tas = {
    tas_init,
    tas_read
};
