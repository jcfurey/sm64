#ifndef GFX_OPENGL_SHADER_H
#define GFX_OPENGL_SHADER_H

#include <stddef.h>

#include "gfx_cc.h"

// Size each of the caller's two buffers must have. The generator does not
// bounds-check as it writes, so this is a contract rather than a hint;
// shader_gen_test sweeps the combiner space and fails if any configuration
// comes close to it.
#define GFX_OPENGL_SHADER_BUF_SIZE 2048

#ifdef __cplusplus
extern "C" {
#endif

// Generates the GLSL for one combiner configuration: a vertex shader into
// vs_buf and a fragment shader into fs_buf, both NUL-terminated, with their
// lengths reported through vs_len_out and fs_len_out. num_floats_out receives
// the vertex stride in floats, which has to agree with what gfx_pc.c writes
// for the same combiner and with the attributes the vertex shader declares.
void gfx_opengl_generate_shader_source(char *vs_buf, char *fs_buf,
                                       size_t *vs_len_out, size_t *fs_len_out,
                                       const struct CCFeatures *cc,
                                       size_t *num_floats_out);

#ifdef __cplusplus
}
#endif

#endif
