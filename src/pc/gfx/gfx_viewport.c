#include "gfx_viewport.h"

static uint32_t subtract_insets(uint32_t size, uint32_t first, uint32_t second) {
    if (first >= size || second >= size - first) {
        return 0;
    }
    return size - first - second;
}

void gfx_viewport_layout_calculate(uint32_t drawable_width,
                                   uint32_t drawable_height,
                                   uint32_t safe_left,
                                   uint32_t safe_top,
                                   uint32_t safe_right,
                                   uint32_t safe_bottom,
                                   bool fit_portrait,
                                   bool retro_mode,
                                   struct GfxViewportLayout *layout) {
    uint32_t safe_width;
    uint32_t safe_height;

    if (drawable_width == 0) {
        drawable_width = 1;
    }
    if (drawable_height == 0) {
        drawable_height = 1;
    }

    layout->x = 0;
    layout->y = 0;
    layout->width = drawable_width;
    layout->height = drawable_height;

    safe_width = subtract_insets(drawable_width, safe_left, safe_right);
    safe_height = subtract_insets(drawable_height, safe_top, safe_bottom);

    if (fit_portrait && safe_width > 0 && safe_height > safe_width) {
        // Portrait uses the top of the safe area as a 4:3 game panel and
        // leaves the remainder as a dedicated touch-control deck. Keeping
        // the original aspect shows the entire scene instead of center-
        // cropping most of it to fill a very tall drawable.
        uint32_t game_width = safe_width;
        uint32_t game_height = game_width * 3 / 4;

        if (game_height > safe_height) {
            game_height = safe_height;
            game_width = game_height * 4 / 3;
        }
        layout->x = safe_left + (safe_width - game_width) / 2;
        layout->y = drawable_height - safe_top - game_height;
        layout->width = game_width;
        layout->height = game_height;
        return;
    }

    if (retro_mode && drawable_width * 3 > drawable_height * 4) {
        // The existing retro presentation stays centered and pillarboxed on
        // wide landscape displays.
        layout->width = drawable_height * 4 / 3;
        layout->x = (drawable_width - layout->width) / 2;
    }
}
