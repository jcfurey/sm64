// In-game options menu, opened by pressing R on the pause screen.
//
// Navigation: stick or D-pad up/down selects an option, left/right or A
// cycles its value, and R, B or Start closes the menu (saving the config).
// Settings take effect immediately and persist in the config file.

#ifndef TARGET_N64

#include <stdio.h>

#include <PR/ultratypes.h>
#include <PR/gbi.h>

#include "sm64.h"
#include "game_init.h"
#include "ingame_menu.h"
#include "segment2.h"
#include "main.h"
#include "options_menu.h"

#include "pc/configfile.h"
#include "pc/controller/controller_touch.h"
#ifdef HIGH_FPS_PC
#include "pc/framerate.h"
#endif

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
#ifdef TARGET_IOS
    OPT_TOUCH_SIZE,
    OPT_TOUCH_ALPHA,
    OPT_TOUCH_EDIT,
#endif
    OPT_LEVEL_SELECT,
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
#ifdef TARGET_IOS
static const char *sChoicesTouchSize[]  = { "SMALL", "NORMAL", "LARGE", "HUGE" };
static const char *sChoicesTouchAlpha[] = { "HIDDEN", "FAINT", "NORMAL", "SOLID" };
static const char *sChoicesEdit[]       = { "PRESS A", "DRAG - TAP TO FINISH" };

// Multipliers behind the size and opacity choices
static const f32 sTouchSizes[]  = { 0.80f, 1.00f, 1.20f, 1.45f };
static const f32 sTouchAlphas[] = { 0.00f, 0.55f, 1.00f, 1.60f };

// Picks the choice index whose multiplier is closest to the stored value,
// so a hand-edited config still shows something sensible
static s32 nearest_choice(const f32 *values, s32 count, f32 value) {
    s32 best = 0;
    f32 bestDist = 1.0e9f;
    s32 i;

    for (i = 0; i < count; i++) {
        f32 d = values[i] - value;
        if (d < 0.0f) {
            d = -d;
        }
        if (d < bestDist) {
            bestDist = d;
            best = i;
        }
    }
    return best;
}
#endif

static const struct OptionDef sOptions[OPT_COUNT] = {
    [OPT_FRAME_CAP]  = { "FRAME RATE",  sChoicesFrameCap, 5 },
    [OPT_VIEW_MODE]  = { "VIEW",        sChoicesViewMode, 3 },
    [OPT_RETRO_MODE] = { "RETRO MODE",  sChoicesOffOn,    2 },
    [OPT_SHOW_FPS]   = { "SHOW FPS",    sChoicesOffOn,    2 },
    [OPT_HUD]        = { "HUD",         sChoicesOnOff,    2 },
#ifdef TARGET_IOS
    [OPT_TOUCH_SIZE]  = { "TOUCH SIZE",  sChoicesTouchSize,  4 },
    [OPT_TOUCH_ALPHA] = { "TOUCH ALPHA", sChoicesTouchAlpha, 4 },
    [OPT_TOUCH_EDIT]  = { "MOVE BUTTONS", sChoicesEdit,      2 },
#endif
    [OPT_LEVEL_SELECT] = { "LEVEL SELECT", sChoicesOffOn,  2 },
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
        case OPT_LEVEL_SELECT: return configLevelSelect;
        case OPT_DEBUG_INFO: return configDebugInfo;
#ifdef TARGET_IOS
        case OPT_TOUCH_SIZE:
            return nearest_choice(sTouchSizes, ARRAY_COUNT(sTouchSizes), configTouchScale);
        case OPT_TOUCH_ALPHA:
            return nearest_choice(sTouchAlphas, ARRAY_COUNT(sTouchAlphas), configTouchOpacity);
        case OPT_TOUCH_EDIT:
            return touch_layout_edit_active();
#endif
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
        case OPT_LEVEL_SELECT:
            // The game's own debug level select, which it has always had
            // but never exposed. It takes effect on the next exit to the
            // castle, so the pause menu's "exit course" leads to it.
            configLevelSelect = value;
            gDebugLevelSelect = value;
            break;
#ifdef TARGET_IOS
        case OPT_TOUCH_SIZE:
            configTouchScale = sTouchSizes[value];
            break;
        case OPT_TOUCH_ALPHA:
            configTouchOpacity = sTouchAlphas[value];
            break;
        case OPT_TOUCH_EDIT:
            touch_layout_edit_set(value != 0);
            break;
#endif
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

// Renders an option's current value. The frame rate the game actually
// reaches is not always the one that was picked: AUTO resolves to whatever
// the display can do, a cap that does not divide the refresh rate is
// rounded down so vsync pacing stays even, and the adaptive backoff lowers
// it further on a device that cannot keep up. Show that number rather than
// letting the menu claim a rate the game is not running at.
static void opt_print_value(s16 x, s16 y, s32 id) {
    const char *selected = sOptions[id].choices[opt_get(id)];

#ifdef HIGH_FPS_PC
    if (id == OPT_FRAME_CAP) {
        char text[OPT_MAX_TEXT];
        s32 actual = 30 * gRenderSubframes;
        s32 chosen = 0;
        const char *c;

        for (c = selected; *c >= '0' && *c <= '9'; c++) {
            chosen = chosen * 10 + (*c - '0');
        }
        if (chosen != actual) {
            // "AUTO 120", or "120 - 60" when the request could not be met
            sprintf(text, chosen == 0 ? "%s %d" : "%s - %d", selected, actual);
            opt_print(x, y, text);
            return;
        }
    }
#endif

    opt_print(x, y, selected);
}

static void optmenu_draw(void) {
    s32 i;

#ifdef TARGET_IOS
    if (touch_layout_edit_active()) {
        // Leave the screen unshaded so the controls being rearranged are
        // actually visible; all that is needed here is the instruction
        gSPDisplayList(gDisplayListHead++, dl_ia_text_begin);
        gDPSetEnvColor(gDisplayListHead++, 255, 255, 120, 255);
        opt_print(40, 200, "DRAG THE BUTTONS");
        opt_print(40, 182, "TAP AN EMPTY SPOT WHEN DONE");
        gSPDisplayList(gDisplayListHead++, dl_ia_text_end);
        return;
    }
#endif

    shade_screen();

    gSPDisplayList(gDisplayListHead++, dl_ia_text_begin);

    gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, 255);
    opt_print(124, 192, "OPTIONS");

    for (i = 0; i < OPT_COUNT; i++) {
        s16 y = 172 - i * 16;

        if (i == sMenuSel) {
            gDPSetEnvColor(gDisplayListHead++, 255, 255, 80, 255);
        } else {
            gDPSetEnvColor(gDisplayListHead++, 200, 200, 200, 255);
        }
        opt_print(60, y, sOptions[i].label);
        opt_print_value(190, y, i);
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
#ifdef TARGET_IOS
    // Leaving the menu while still rearranging would strand the player in a
    // mode with no way out; turning it off here also saves the layout
    touch_layout_edit_set(FALSE);
#endif
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
