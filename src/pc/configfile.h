#ifndef CONFIGFILE_H
#define CONFIGFILE_H

#include <stdbool.h>

extern bool         configFullscreen;
// Frame rate cap: 0 = match the display, otherwise 30/60/90/120
extern unsigned int configFrameCap;
// 0 = normal, 1 = wireframe, 2 = collision surfaces
extern unsigned int configViewMode;
// Authentic 4:3, 320x240-style, 30 fps presentation
extern bool         configRetroMode;
extern bool         configShowFPS;
extern bool         configHUD;
extern bool         configDebugInfo;
extern unsigned int configKeyA;
extern unsigned int configKeyB;
extern unsigned int configKeyStart;
extern unsigned int configKeyR;
extern unsigned int configKeyZ;
extern unsigned int configKeyCUp;
extern unsigned int configKeyCDown;
extern unsigned int configKeyCLeft;
extern unsigned int configKeyCRight;
extern unsigned int configKeyStickUp;
extern unsigned int configKeyStickDown;
extern unsigned int configKeyStickLeft;
extern unsigned int configKeyStickRight;

void configfile_load(const char *filename);
void configfile_save(const char *filename);
// Saves to the standard config path (for the in-game options menu)
void configfile_save_current(void);

#endif
