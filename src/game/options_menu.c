// In-game options menu, opened by pressing R on the pause screen.
//
// Navigation: stick or D-pad up/down selects an option, left/right or A
// cycles its value, and R, B or Start closes the menu (saving the config).
// Settings take effect immediately and persist in the config file.

#ifndef TARGET_N64

#include <PR/ultratypes.h>
#include <PR/gbi.h>

#include "sm64.h"
#include "game_init.h"
#include "ingame_menu.h"
#include "segment2.h"
#include "main.h"
#include "options_menu.h"

#include "pc/configfile.h"

// Defined in ingame_menu.c but not declared in its header
void shade_screen(void);
extern s16 gMenuMode;

#define OPT_MAX_TEXT 32

enum OptionId {
    OPT_FRAME_CAP,
    OPT_VIEW_MODE,
    OPT_RETRO_MODE,
    OPT_SHOW_FPS,
    OPT_HUD,
    OPT_DEBUG_INFO,
    OPT_COUNT
};

struct OptionDef {
    const char *label;
    const char **choices;
    s32 numChoices;
};

static const char *sChoicesFrameCap[] = { "AUTO", "30", "60", "90", "120" };
static const char *sChoicesViewMode[] = { "NORMAL", "WIREFRAME", "COLLISION" };
static const char *sChoicesOffOn[]    = { "OFF", "ON" };
static const char *sChoicesOnOff[]    = { "ON", "OFF" };

static const struct OptionDef sOptions[OPT_COUNT] = {
    [OPT_FRAME_CAP]  = { "FRAME RATE",  sChoicesFrameCap, 5 },
    [OPT_VIEW_MODE]  = { "VIEW",        sChoicesViewMode, 3 },
    [OPT_RETRO_MODE] = { "RETRO MODE",  sChoicesOffOn,    2 },
    [OPT_SHOW_FPS]   = { "SHOW FPS",    sChoicesOffOn,    2 },
    [OPT_HUD]        = { "HUD",         sChoicesOnOff,    2 },
    [OPT_DEBUG_INFO] = { "DEBUG INFO",  sChoicesOffOn,    2 },
};

static s32 sMenuOpen = FALSE;
static s32 sMenuSel = 0;
static s32 sStickWasNeutral = TRUE;

//------------------------------------------------------------------------------
// Option values <-> config
//------------------------------------------------------------------------------

static s32 opt_get(s32 id) {
    switch (id) {
        case OPT_FRAME_CAP:
            switch (configFrameCap) {
                case 30:  return 1;
                case 60:  return 2;
                case 90:  return 3;
                case 120: return 4;
                default:  return 0;
            }
        case OPT_VIEW_MODE:  return configViewMode > 2 ? 0 : (s32) configViewMode;
        case OPT_RETRO_MODE: return configRetroMode;
        case OPT_SHOW_FPS:   return configShowFPS;
        case OPT_HUD:        return !configHUD;
        case OPT_DEBUG_INFO: return configDebugInfo;
    }
    return 0;
}

static void opt_set(s32 id, s32 value) {
    static const unsigned int frameCaps[] = { 0, 30, 60, 90, 120 };

    switch (id) {
        case OPT_FRAME_CAP:
            configFrameCap = frameCaps[value];
            break;
        case OPT_VIEW_MODE:
            configViewMode = value;
            break;
        case OPT_RETRO_MODE:
            configRetroMode = value;
            break;
        case OPT_SHOW_FPS:
            configShowFPS = value;
            break;
        case OPT_HUD:
            configHUD = !value;
            break;
        case OPT_DEBUG_INFO:
            configDebugInfo = value;
            gShowDebugText = value;
            gShowProfiler = value;
            break;
    }
}

static void opt_cycle(s32 id, s32 dir) {
    const struct OptionDef *def = &sOptions[id];
    s32 value = opt_get(id) + dir;

    if (value < 0) {
        value = def->numChoices - 1;
    } else if (value >= def->numChoices) {
        value = 0;
    }
    opt_set(id, value);
}

//------------------------------------------------------------------------------
// Text rendering
//------------------------------------------------------------------------------

// Converts ASCII to the dialog font's character codes
static void ascii_to_dialog(u8 *dst, const char *src) {
    s32 i = 0;

    while (*src != '\0' && i < OPT_MAX_TEXT - 1) {
        char c = *src++;
        if (c >= '0' && c <= '9') {
            dst[i++] = c - '0';
        } else if (c >= 'A' && c <= 'Z') {
            dst[i++] = c - 'A' + 0x0A;
        } else if (c >= 'a' && c <= 'z') {
            dst[i++] = c - 'a' + 0x24;
        } else if (c == '.') {
            dst[i++] = 0x3F;
        } else if (c == '-') {
            dst[i++] = 0x9F;
        } else {
            dst[i++] = 0x9E; // space
        }
    }
    dst[i] = 0xFF; // terminator
}

static void opt_print(s16 x, s16 y, const char *str) {
    u8 buf[OPT_MAX_TEXT];
    ascii_to_dialog(buf, str);
    print_generic_string(x, y, buf);
}

static void optmenu_draw(void) {
    s32 i;

    shade_screen();

    gSPDisplayList(gDisplayListHead++, dl_ia_text_begin);

    gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, 255);
    opt_print(124, 192, "OPTIONS");

    for (i = 0; i < OPT_COUNT; i++) {
        s16 y = 168 - i * 18;

        if (i == sMenuSel) {
            gDPSetEnvColor(gDisplayListHead++, 255, 255, 80, 255);
        } else {
            gDPSetEnvColor(gDisplayListHead++, 200, 200, 200, 255);
        }
        opt_print(60, y, sOptions[i].label);
        opt_print(190, y, sOptions[i].choices[opt_get(i)]);
    }

    gDPSetEnvColor(gDisplayListHead++, 160, 160, 160, 255);
    opt_print(64, 46, "R BACK   A CHANGE");

    gSPDisplayList(gDisplayListHead++, dl_ia_text_end);
}

//------------------------------------------------------------------------------
// Input and entry point
//------------------------------------------------------------------------------

static void optmenu_close(void) {
    sMenuOpen = FALSE;
    configfile_save_current();
}

s32 optmenu_update_and_render(void) {
    u16 pressed = gPlayer1Controller->buttonPressed;
    s16 stickY = gPlayer1Controller->rawStickY;
    s16 stickX = gPlayer1Controller->rawStickX;

    if (!sMenuOpen) {
        if (gMenuMode == MENU_MODE_RENDER_PAUSE_SCREEN && (pressed & R_TRIG)) {
            sMenuOpen = TRUE;
            sMenuSel = 0;
            sStickWasNeutral = FALSE;
        } else {
            return FALSE;
        }
    } else {
        if (pressed & (R_TRIG | B_BUTTON | START_BUTTON)) {
            optmenu_close();
        } else {
            if (stickY > -20 && stickY < 20 && stickX > -20 && stickX < 20
                && !(pressed & (U_JPAD | D_JPAD | L_JPAD | R_JPAD))) {
                sStickWasNeutral = TRUE;
            } else if (sStickWasNeutral) {
                sStickWasNeutral = FALSE;
                if (stickY > 40 || (pressed & U_JPAD)) {
                    sMenuSel = sMenuSel == 0 ? OPT_COUNT - 1 : sMenuSel - 1;
                } else if (stickY < -40 || (pressed & D_JPAD)) {
                    sMenuSel = (sMenuSel + 1) % OPT_COUNT;
                } else if (stickX < -40 || (pressed & L_JPAD)) {
                    opt_cycle(sMenuSel, -1);
                } else if (stickX > 40 || (pressed & R_JPAD)) {
                    opt_cycle(sMenuSel, 1);
                }
            }
            if (pressed & A_BUTTON) {
                opt_cycle(sMenuSel, 1);
            }
        }
    }

    // Swallow this frame's input so the pause menu underneath ignores it
    gPlayer1Controller->buttonPressed = 0;
    gPlayer3Controller->buttonPressed = 0;

    if (sMenuOpen) {
        optmenu_draw();
    }
    return TRUE;
}

#endif
