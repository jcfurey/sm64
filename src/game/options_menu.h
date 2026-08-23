#ifndef OPTIONS_MENU_H
#define OPTIONS_MENU_H

#ifndef TARGET_N64

#include <PR/ultratypes.h>

// Frames per second over the last second. iOS Metal counts confirmed onscreen
// presentations so dropped frames are visible instead of being reported as
// successful submissions.
extern s32 gCurrentFPS;

// Clears the one-second presentation window after a cap change or scene
// transition so the menu never labels the new state with stale samples.
void fps_counter_reset(void);

// Updates and renders the options menu and its Debug Features submenu. Opened
// by pressing R on the pause screen. Returns TRUE while a menu is open, in
// which case the pause menu underneath must not process this frame's input or
// render.
s32 optmenu_update_and_render(void);

#endif

#endif
