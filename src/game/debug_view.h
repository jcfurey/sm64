#ifndef DEBUG_VIEW_H
#define DEBUG_VIEW_H

#ifndef TARGET_N64

// When the collision view mode is selected, appends a display list drawing
// the collision surfaces near Mario as translucent colored triangles
// (floors green, ceilings red, walls blue). Must be called during geo
// processing while the camera matrix is on the matrix stack and a master
// list is active.
void debug_view_append_collision(void);

#endif

#endif
