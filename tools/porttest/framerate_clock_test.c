#include <stdint.h>
#include <stdio.h>

#include "framerate.h"

int main(void) {
    uint64_t timestamp = 0;
    // Change caps between logic ticks. The timestamp's scale must stay fixed
    // and each full tick must still take 1/GAME_FRAMERATE seconds.
    const int counts[] = { 2, 1, 4, 1, 3, 2 };
    for (unsigned tick = 0; tick < sizeof(counts) / sizeof(counts[0]); tick++) {
        int count = 1;
#ifdef HIGH_FPS_PC
        count = counts[tick];
        gRenderSubframes = count;
#endif
        for (int frame = 0; frame < count; frame++) {
            timestamp += framerate_frame_interval_units();
        }
        uint64_t elapsed_us = timestamp / FRAMERATE_CLOCK_DENOMINATOR;
        uint64_t expected_us = (tick + 1) * 1000000ULL / GAME_FRAMERATE;
        if (elapsed_us != expected_us) {
            fprintf(stderr, "FAIL: tick %u at %d subframes: %llu us, expected %llu\n",
                    tick, count, (unsigned long long) elapsed_us, (unsigned long long) expected_us);
            return 1;
        }
    }
    printf("presentation clock: cap changes preserve %d Hz logic\n", GAME_FRAMERATE);
    return 0;
}
