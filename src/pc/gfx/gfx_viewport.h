#ifndef GFX_VIEWPORT_H
#define GFX_VIEWPORT_H

#include <stdbool.h>
#include <stdint.h>

// Rectangle used by the N64 renderer inside the native drawable. x/y use
// the rendering backends' bottom-left origin; UIKit's safe_top is converted
// by gfx_viewport_layout_calculate().
struct GfxViewportLayout {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
};

void gfx_viewport_layout_calculate(uint32_t drawable_width,
                                   uint32_t drawable_height,
                                   uint32_t safe_left,
                                   uint32_t safe_top,
                                   uint32_t safe_right,
                                   uint32_t safe_bottom,
                                   bool fit_portrait,
                                   bool retro_mode,
                                   struct GfxViewportLayout *layout);

#endif
