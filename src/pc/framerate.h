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

// Set by window backends that present at a fixed rate and cannot honor a
// lower sub-frame count (glx, dxgi)
extern s32 gSubframesLocked;

// Maximum supported sub-frames per logic frame (120 Hz displays)
#define MAX_SUBFRAMES 4
// Maximum interpolated (non-final) variants
#define MAX_INTERP_FRAMES (MAX_SUBFRAMES - 1)

// Interpolation fraction of variant v for the current sub-frame count
#define INTERP_FACTOR(v) (((f32)(v) + 1.0f) / (f32) gRenderSubframes)

#endif
