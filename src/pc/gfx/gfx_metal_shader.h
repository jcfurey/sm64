#ifndef GFX_METAL_SHADER_H
#define GFX_METAL_SHADER_H

#include <stddef.h>

#include "gfx_cc.h"

// Size the caller's buffer must have. The generator does not bounds-check as
// it writes, so this is a contract rather than a hint; metal_shader_test
// sweeps the combiner space and fails if any configuration comes close to it.
#define GFX_METAL_SHADER_BUF_SIZE 8192

#ifdef __cplusplus
extern "C" {
#endif

// Generates the Metal shading language for one combiner configuration into
// buf, returns the length written, and reports the vertex stride in floats
// through num_floats_out. That stride has to agree with what gfx_pc.c writes
// for the same combiner, and with the STRIDE constant baked into the emitted
// source; the fallback pipeline depends on it too.
size_t gfx_metal_generate_shader_source(char *buf, struct CCFeatures *cc, size_t *num_floats_out);

#ifdef __cplusplus
}
#endif

#endif
