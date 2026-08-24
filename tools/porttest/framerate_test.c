// Tests the adaptive sub-frame backoff in src/pc/framerate.c.
//
// Sub-frames are rendered inside the same 1/30 s the game logic runs in, so
// a device that cannot draw them all falls behind on logic frames and the
// game runs in slow motion. The policy watches frame timings and lowers the
// ceiling before that happens. Reproducing a thermally throttled phone on
// demand is impractical, so the policy is driven here with synthetic
// timings instead.

#include <stdio.h>

#include "framerate.h"
#include "configfile.h"

static int failures;
static long long now;

#define CHECK(cond, what)                                                     \
    do {                                                                      \
        if (!(cond)) {                                                        \
            printf("  FAIL: %s (max=%d)\n", what, framerate_adaptive_max());  \
            failures++;                                                       \
        } else {                                                              \
            printf("  ok:   %-38s max=%d\n", what, framerate_adaptive_max()); \
        }                                                                     \
    } while (0)

// Feeds the policy 'frames' logic frames that each took 'period_ms'
static void run(int frames, int period_ms) {
    int i;
    for (i = 0; i < frames; i++) {
        now += period_ms;
        framerate_note_logic_frame(now);
    }
}

static void reset(void) {
    framerate_reset();
    now = 0;
}

int main(void) {
    printf("adaptive sub-frame backoff\n");

    // A display being kept up with never costs the player a sub-frame
    reset(); run(2000, 33);
    CHECK(framerate_adaptive_max() == MAX_SUBFRAMES, "steady 33 ms holds the ceiling");

    // Frames slightly over budget are still within the allowed headroom
    reset(); run(2000, 39);
    CHECK(framerate_adaptive_max() == MAX_SUBFRAMES, "steady 39 ms is not treated as late");

    // Sustained overload walks the ceiling all the way down
    reset(); run(2000, 55);
    CHECK(framerate_adaptive_max() == 1, "sustained 55 ms backs off to 1");

    // A level load is a discontinuity, not the renderer failing to keep up
    reset(); run(50, 33); now += 900; framerate_note_logic_frame(now); run(50, 33);
    CHECK(framerate_adaptive_max() == MAX_SUBFRAMES, "a 900 ms load stall is ignored");

    // Isolated hitches must not accumulate into a permanent downgrade
    reset();
    { int i; for (i = 0; i < 100; i++) { run(90, 33); run(1, 50); } }
    CHECK(framerate_adaptive_max() == MAX_SUBFRAMES, "occasional hitches are forgiven");

    // A device that recovers earns its sub-frames back
    reset(); run(2000, 55);
    CHECK(framerate_adaptive_max() == 1, "overloaded, then...");
    run(3000, 33);
    CHECK(framerate_adaptive_max() == MAX_SUBFRAMES, "...a clean stretch restores it");

    printf("\npresentation feedback\n");

    reset();
    framerate_note_presented_rate(60, 120);
    CHECK(framerate_adaptive_max() == 2, "60 presented from 120 requests backs off");
    framerate_resume();
    CHECK(framerate_adaptive_max() == 2, "resume preserves the learned ceiling");
    { int i; for (i = 0; i < 10; i++) framerate_note_presented_rate(60, 60); }
    CHECK(framerate_adaptive_max() == 3, "ten clean windows allow a cautious probe");
    framerate_note_presented_rate(60, 120);
    CHECK(framerate_adaptive_max() == 2, "a failed probe backs off immediately");

    // However bad it gets, one render per logic frame is the floor
    reset(); run(50000, 150);
    CHECK(framerate_adaptive_max() == 1, "catastrophic load still floors at 1");

    printf("\nsub-frame selection\n");

    // The cap must divide the display multiple or vsync pacing is uneven,
    // so 90 fps on a 120 Hz display has to round down rather than tear
    reset();
    gMaxSubframes = 4;
    gSubframesLocked = 0;
    configRetroMode = 0;
    configFrameCap = 0;
    CHECK(framerate_choose_subframes() == 4, "auto uses the whole display rate");
    configFrameCap = 60;
    CHECK(framerate_choose_subframes() == 2, "a 60 cap on a 120 Hz display");
    configFrameCap = 90;
    CHECK(framerate_choose_subframes() == 2, "a 90 cap rounds down to a divisor");
    configFrameCap = 240;
    CHECK(framerate_choose_subframes() == 4, "a cap above the display is clamped");
    configFrameCap = 0;
    configRetroMode = 1;
    CHECK(framerate_choose_subframes() == 1, "retro mode locks to 30 fps");
    configRetroMode = 0;

    // Backends that present at a fixed interval rely on the sub-frame count
    // for correct game speed, so the policy must leave them alone
    reset(); run(2000, 55);
    gSubframesLocked = 1;
    gMaxSubframes = 2;
    CHECK(framerate_choose_subframes() == 2, "a locked backend ignores the backoff");
    gSubframesLocked = 0;

    printf("\nplatform-reported ceiling\n");

    // Thermal pressure and Low Power Mode are reported before the device
    // actually throttles. The measured backoff can only react after frames
    // are already late, so the platform ceiling has to bound the choice too.
    reset();
    framerate_set_platform_ceiling(MAX_SUBFRAMES);
    gMaxSubframes = 4;
    gSubframesLocked = 0;
    configRetroMode = 0;
    configFrameCap = 0;
    CHECK(framerate_choose_subframes() == 4, "unconstrained keeps the full rate");

    framerate_set_platform_ceiling(2);
    CHECK(framerate_choose_subframes() == 2, "a serious thermal state halves it");

    framerate_set_platform_ceiling(1);
    CHECK(framerate_choose_subframes() == 1, "a critical thermal state pins 30 fps");

    // A user asking for less than the platform allows still gets less
    framerate_set_platform_ceiling(4);
    configFrameCap = 30;
    CHECK(framerate_choose_subframes() == 1, "the user cap still wins when lower");
    configFrameCap = 0;

    // Out-of-range values from a future platform mapping must not widen the
    // ceiling past what the interpolation code supports
    framerate_set_platform_ceiling(99);
    CHECK(framerate_platform_ceiling() == MAX_SUBFRAMES, "an absurd ceiling clamps down");
    framerate_set_platform_ceiling(-5);
    CHECK(framerate_platform_ceiling() == 1, "a nonsense ceiling clamps up to 1");

    // Resuming from the background discards frame timings, but the device is
    // just as hot as it was a moment ago
    framerate_set_platform_ceiling(2);
    framerate_reset();
    CHECK(framerate_platform_ceiling() == 2, "resume does not forget thermal pressure");
    CHECK(framerate_choose_subframes() == 2, "and the ceiling still applies after reset");
    framerate_set_platform_ceiling(MAX_SUBFRAMES);

    printf(failures == 0 ? "\nPASS\n" : "\nFAILED (%d)\n", failures);
    return failures != 0;
}
