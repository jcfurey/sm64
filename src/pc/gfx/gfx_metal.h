#ifndef GFX_METAL_H
#define GFX_METAL_H

#ifdef ENABLE_METAL

#include <stdint.h>

#include "gfx_rendering_api.h"

#ifdef __cplusplus
extern "C" {
#endif

extern struct GfxRenderingAPI gfx_metal_api;

// Called by the window backend once per frame after gfx_run finished
// (schedules the present and commits the frame's command buffer)
void gfx_metal_present(void);

// Total drawables Metal has confirmed were displayed onscreen on a physical
// iOS device. Dropped presentations are excluded so the device FPS counter
// reports what the player actually saw rather than CPU submissions.
uint64_t gfx_metal_presented_frame_count(void);

#ifdef __cplusplus
}
#endif

#endif

#endif
