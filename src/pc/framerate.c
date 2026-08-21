// Frame pacing policy: how many times each logic frame gets rendered.
//
// This lives apart from the main loop so the policy can be exercised
// directly by tools/porttest/framerate_test.c, which drives it with
// synthetic frame timings that would be impractical to reproduce by
// playing the game on a struggling device.

#include <time.h>

#include "framerate.h"
#include "configfile.h"

// Milliseconds from a clock that only moves forward. Both the frame rate
// counter and the sub-frame backoff measure intervals with this, so it must
// not jump when the system clock is adjusted.
long long framerate_monotonic_ms(void) {
#if defined(CLOCK_MONOTONIC)
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (long long) ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    }
#else
    struct timespec ts;
    if (timespec_get(&ts, TIME_UTC) == TIME_UTC) {
        return (long long) ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    }
#endif
    return (long long) time(NULL) * 1000;
}

#ifdef HIGH_FPS_PC

// Frames rendered per logic frame; latched at the start of each logic frame
// so it never changes partway through one
s32 gRenderSubframes = 1;
s32 gMaxSubframes = 1;
s32 gSubframesLocked = 0;

//------------------------------------------------------------------------------
// Adaptive sub-frame backoff
//
// Sub-frames are rendered inside the same 1/30 s that the game logic runs
// in, so a device that cannot draw them all does not simply show fewer
// frames: the logic frames themselves start arriving late and the whole
// game runs in slow motion. Watch how long each logic frame actually takes
// and give up a sub-frame before that happens, earning it back only after a
// long clean stretch so one hitch does not cost the frame rate for good.
//------------------------------------------------------------------------------

// A logic frame should take 1/30 s; allow some headroom before calling it late
#define FRAME_LATE_MS 40

// Past this a frame is a discontinuity (level load, resuming from the
// background) rather than the renderer failing to keep up
#define FRAME_STALL_MS 200

// Late frames needed to give up a sub-frame, clean frames needed to earn one
// back, and how often a clean stretch forgives an earlier late frame
#define LATE_BUDGET 15
#define RECOVER_FRAMES 300
#define FORGIVE_INTERVAL 60

static s32 sAdaptiveMax = MAX_SUBFRAMES;
static s32 sLateFrames;
static s32 sGoodStreak;
static long long sPrevFrameStartMs;

void framerate_note_logic_frame(long long frame_start) {
    long long period = frame_start - sPrevFrameStartMs;

    sPrevFrameStartMs = frame_start;
    if (period <= 0 || period >= FRAME_STALL_MS) {
        // First frame, or something other than rendering held us up
        return;
    }

    if (period > FRAME_LATE_MS) {
        sGoodStreak = 0;
        if (++sLateFrames >= LATE_BUDGET) {
            sLateFrames = 0;
            if (sAdaptiveMax > 1) {
                sAdaptiveMax--;
            }
        }
        return;
    }

    sGoodStreak++;
    if (sGoodStreak % FORGIVE_INTERVAL == 0 && sLateFrames > 0) {
        sLateFrames--;
    }
    if (sGoodStreak >= RECOVER_FRAMES) {
        sGoodStreak = 0;
        sLateFrames = 0;
        if (sAdaptiveMax < MAX_SUBFRAMES) {
            sAdaptiveMax++;
        }
    }
}

// Discards accumulated pacing history. Called when the app returns from the
// background, where the measurements either side of the gap say nothing
// about how well the device is keeping up.
void framerate_reset(void) {
    sAdaptiveMax = MAX_SUBFRAMES;
    sLateFrames = 0;
    sGoodStreak = 0;
    sPrevFrameStartMs = 0;
}

s32 framerate_adaptive_max(void) {
    return sAdaptiveMax;
}

// Chooses this frame's sub-frame count: the user's frame cap and retro
// mode lower it, the display capability and measured headroom bound it, and
// it must divide the display multiple so vsync pacing stays even
s32 framerate_choose_subframes(void) {
    s32 max = gMaxSubframes < 1 ? 1 : (gMaxSubframes > MAX_SUBFRAMES ? MAX_SUBFRAMES : gMaxSubframes);
    s32 want = max;

    if (gSubframesLocked) {
        // The window backend swaps at a fixed interval, so the sub-frame
        // count is what keeps the game running at the right speed and must
        // not be second-guessed here
        return max;
    }
    if (configFrameCap != 0) {
        want = configFrameCap / 30;
    }
    if (configRetroMode) {
        want = 1;
    }
    if (want < 1) {
        want = 1;
    }
    if (want > max) {
        want = max;
    }
    if (want > sAdaptiveMax) {
        want = sAdaptiveMax;
    }
    while (want > 1 && max % want != 0) {
        want--;
    }
    return want;
}

#endif
