#include "gfx_viewport.h"

static uint32_t subtract_insets(uint32_t size, uint32_t first, uint32_t second) {
    if (first >= size || second >= size - first) {
        return 0;
    }
    return size - first - second;
}

static uint32_t overlap_at_start(uint32_t viewport_start, uint32_t viewport_size,
                                 uint32_t safe_start) {
    uint32_t overlap;

    if (safe_start <= viewport_start) {
        return 0;
    }
    overlap = safe_start - viewport_start;
    return overlap < viewport_size ? overlap : viewport_size;
}

static uint32_t overlap_at_end(uint32_t drawable_size, uint32_t viewport_start,
                               uint32_t viewport_size, uint32_t safe_end_inset) {
    uint64_t viewport_end = (uint64_t) viewport_start + viewport_size;
    uint32_t safe_end = safe_end_inset < drawable_size
        ? drawable_size - safe_end_inset : 0;
    uint64_t overlap;

    if (viewport_end <= safe_end) {
        return 0;
    }
    overlap = viewport_end - safe_end;
    return overlap < viewport_size ? (uint32_t) overlap : viewport_size;
}

static void calculate_overlapping_safe_insets(uint32_t drawable_width,
                                               uint32_t drawable_height,
                                               uint32_t safe_left,
                                               uint32_t safe_top,
                                               uint32_t safe_right,
                                               uint32_t safe_bottom,
                                               struct GfxViewportLayout *layout) {
    layout->safe_left = overlap_at_start(layout->x, layout->width, safe_left);
    layout->safe_right = overlap_at_end(drawable_width, layout->x, layout->width, safe_right);
    // Viewport Y uses a bottom-left origin, while safe_top is measured from
    // UIKit's top edge.
    layout->safe_bottom = overlap_at_start(layout->y, layout->height, safe_bottom);
    layout->safe_top = overlap_at_end(drawable_height, layout->y, layout->height, safe_top);
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
    layout->safe_left = 0;
    layout->safe_top = 0;
    layout->safe_right = 0;
    layout->safe_bottom = 0;

    safe_width = subtract_insets(drawable_width, safe_left, safe_right);
    safe_height = subtract_insets(drawable_height, safe_top, safe_bottom);
    if (safe_width == 0) {
        safe_left = 0;
        safe_right = 0;
    }
    if (safe_height == 0) {
        safe_top = 0;
        safe_bottom = 0;
    }

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
    } else if (retro_mode && (uint64_t) drawable_width * 3 > (uint64_t) drawable_height * 4) {
        // The existing retro presentation stays centered and pillarboxed on
        // wide landscape displays.
        layout->width = drawable_height * 4 / 3;
        layout->x = (drawable_width - layout->width) / 2;
    }

    calculate_overlapping_safe_insets(drawable_width, drawable_height,
                                      safe_left, safe_top, safe_right, safe_bottom,
                                      layout);
}
