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
// On batch-driven backends, a device that cannot draw all requested sub-frames
// can delay logic and make the whole game run in slow motion. The iOS
// display-linked backend submits only one drawable per callback, but its logic
// timing can still expose CPU overload. Watch the logic clock on both paths and
// give up a sub-frame after sustained lateness; presentation feedback below
// independently catches frames Core Animation drops after submission.
//------------------------------------------------------------------------------

// A logic frame should take 1/GAME_FRAMERATE seconds; allow a fifth of that
// again as headroom before calling one late
#define FRAME_PERIOD_MS (1000 / GAME_FRAMERATE)
#define FRAME_LATE_MS (FRAME_PERIOD_MS + FRAME_PERIOD_MS / 5)

// Past this a frame is a discontinuity (level load, resuming from the
// background) rather than the renderer failing to keep up
#define FRAME_STALL_MS 200

// Late frames needed to give up a sub-frame, clean frames needed to earn one
// back, and how often a clean stretch forgives an earlier late frame
#define LATE_BUDGET 15
#define RECOVER_FRAMES 300
#define FORGIVE_INTERVAL 60

static s32 sAdaptiveMax = MAX_SUBFRAMES;
static s32 sPresentationMax = MAX_SUBFRAMES;
static s32 sLateFrames;
static s32 sGoodStreak;
static s32 sPresentationGoodWindows;
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
    sPresentationMax = MAX_SUBFRAMES;
    sLateFrames = 0;
    sGoodStreak = 0;
    sPresentationGoodWindows = 0;
    sPrevFrameStartMs = 0;
}

void framerate_resume(void) {
    // Keep ceilings learned from this physical device. Only timing samples
    // spanning the suspension are invalid; immediately retrying 120 Hz after
    // every notification or lock-screen visit recreates the thermal load.
    sLateFrames = 0;
    sGoodStreak = 0;
    sPresentationGoodWindows = 0;
    sPrevFrameStartMs = 0;
}

void framerate_note_presented_rate(s32 presented_fps, s32 requested_fps) {
    const s32 tolerance_fps = 3;
    const s32 recovery_windows = 10;

    if (presented_fps < 0 || requested_fps < GAME_FRAMERATE) {
        return;
    }

    if (presented_fps + tolerance_fps < requested_fps) {
        s32 sustainable = (presented_fps + tolerance_fps) / GAME_FRAMERATE;
        if (sustainable < 1) {
            sustainable = 1;
        }
        if (sustainable < sPresentationMax) {
            sPresentationMax = sustainable;
        }
        sPresentationGoodWindows = 0;
        return;
    }

    // Once the current rate has been delivered cleanly for ten seconds, allow
    // a cautious probe upward. A failed probe is corrected by the next window.
    if (sPresentationMax < MAX_SUBFRAMES
        && ++sPresentationGoodWindows >= recovery_windows) {
        sPresentationGoodWindows = 0;
        sPresentationMax++;
    }
}

s32 framerate_adaptive_max(void) {
    return sAdaptiveMax < sPresentationMax ? sAdaptiveMax : sPresentationMax;
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
        want = configFrameCap / GAME_FRAMERATE;
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
    if (want > sPresentationMax) {
        want = sPresentationMax;
    }
    while (want > 1 && max % want != 0) {
        want--;
    }
    return want;
}

#endif
