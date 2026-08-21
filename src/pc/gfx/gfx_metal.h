#ifndef GFX_METAL_H
#define GFX_METAL_H

#ifdef ENABLE_METAL

#include "gfx_rendering_api.h"

#ifdef __cplusplus
extern "C" {
#endif

extern struct GfxRenderingAPI gfx_metal_api;

// Called by the window backend once per frame after gfx_run finished
// (schedules the present and commits the frame's command buffer)
void gfx_metal_present(void);

#ifdef __cplusplus
}
#endif

#endif

#endif
