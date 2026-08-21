#ifndef CONTROLLER_TOUCH_H
#define CONTROLLER_TOUCH_H

#ifdef TARGET_IOS

#include "controller_api.h"

extern struct ControllerAPI controller_touch;

// Fed by the window backend (gfx_sdl2.c) from SDL touch events.
// x and y are normalized coordinates in [0, 1] relative to the window.
void touch_down(long long finger_id, float x, float y);
void touch_motion(long long finger_id, float x, float y);
void touch_up(long long finger_id);

// Tells the touch layer the drawable size in pixels, so hit testing is
// aspect-correct before the first overlay frame is drawn
void touch_set_screen_size(int width, int height);

// Builds the overlay geometry for the current touch state and returns it:
// *num_verts vertices, interleaved [x, y, r, g, b, a] with x/y in
// normalized device coordinates. The Metal backend draws this itself.
const float *touch_overlay_build(int width, int height, int *num_verts);

#ifdef ENABLE_OPENGL
// Draws the on-screen control overlay. Called by the window backend at the
// end of a frame, with the GL context current and the frame already
// rendered. Width/height are the drawable size in pixels.
void touch_render_overlay(int width, int height);
#endif

#endif

#endif
