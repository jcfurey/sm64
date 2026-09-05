#ifndef PC_FRAMERATE_H
#define PC_FRAMERATE_H

#include <PR/ultratypes.h>

// Frame interpolation: the game logic always runs at its native rate
// (30 Hz, 25 Hz for the EU version), but each logic frame can be rendered
// several times with object transforms, camera, animations and effects
// sampled between the previous and current game state.
//
// gRenderSubframes is the number of frames rendered per logic frame,
// chosen by the window backend from the display refresh rate:
//   60 Hz display -> 2, 90 Hz -> 3, 120 Hz -> 4, otherwise 1.
// It is latched at the start of each logic frame; a value of 1 renders
// exactly like the vanilla port.
//
// Within one logic frame there are gRenderSubframes render variants,
// numbered v = 0 .. gRenderSubframes-1. Variant v samples the game state
// at fraction (v + 1) / gRenderSubframes of the way from the previous
// logic frame to the current one, so the last variant is always the
// exact current state. The display list is built referencing variant 0;
// before each later sub-frame is rendered, patch functions rewrite the
// display list for the next variant.

extern s32 gRenderSubframes;

// Display capability: the most sub-frames per logic frame the display
// refresh rate supports (set once by the window backend at init)
extern s32 gMaxSubframes;

// The rate the game's logic runs at, which the EU release halves from the
// 50 Hz PAL field rate rather than the 60 Hz NTSC one. Everything that
// converts between frame caps, sub-frame counts and real time has to go
// through this rather than assuming 30.
#ifdef VERSION_EU
#define GAME_FRAMERATE 25
#else
#define GAME_FRAMERATE 30
#endif

// Maximum supported sub-frames per logic frame (120 Hz displays)
#define MAX_SUBFRAMES 4
// Maximum interpolated (non-final) variants
#define MAX_INTERP_FRAMES (MAX_SUBFRAMES - 1)

// Interpolation fraction of variant v for the current sub-frame count
#define INTERP_FACTOR(v) (((f32)(v) + 1.0f) / (f32) gRenderSubframes)

// Desktop presentation timestamps use a fixed fractional-microsecond unit.
// Twelve divides evenly by every supported sub-frame count (1..4), so a
// complete logic tick always advances the same time even when the cap changes.
#define FRAMERATE_CLOCK_DENOMINATOR (GAME_FRAMERATE * 12)
u32 framerate_frame_interval_units(void);

// Milliseconds from a clock that only moves forward, for measuring intervals
long long framerate_monotonic_ms(void);

// Records the start of a logic frame so the pacing policy can see whether
// the device is keeping up, and picks the sub-frame count for the frame
// about to be produced. See src/pc/framerate.c.
void framerate_note_logic_frame(long long frame_start_ms);
s32 framerate_choose_subframes(void);

// Throws away accumulated pacing history (used when resuming from the
// background, where the gap says nothing about rendering performance)
void framerate_reset(void);

// Discards only samples invalidated by a suspension, preserving the maximum
// rate already learned for this device.
void framerate_resume(void);

// Feeds the policy the one-second on-screen presentation result. This catches
// Core Animation drops that logic-start timing alone cannot observe.
void framerate_note_presented_rate(s32 presented_fps, s32 requested_fps);

// The current ceiling the backoff has settled on; exposed for tests and
// diagnostics rather than for the game to act on
s32 framerate_adaptive_max(void);

// A ceiling in sub-frames reported by the platform rather than measured from
// frame timings -- on iOS, thermal pressure and Low Power Mode. Anticipating
// a throttle avoids the stutter that the measured backoff can only react to.
// MAX_SUBFRAMES means unconstrained. Unaffected by framerate_reset.
void framerate_set_platform_ceiling(s32 max_subframes);
s32 framerate_platform_ceiling(void);

#endif
