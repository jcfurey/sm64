#ifndef OPTIONS_MENU_H
#define OPTIONS_MENU_H

#ifndef TARGET_N64

#include <PR/ultratypes.h>

// Rendered frames per second over the last second, measured by the
// platform layer (pc_main.c)
extern s32 gCurrentFPS;

// Updates and renders the options menu. Opened by pressing R on the pause
// screen. Returns TRUE while the menu is open, in which case the pause menu
// underneath must not process this frame's input or render.
s32 optmenu_update_and_render(void);

#endif

#endif
