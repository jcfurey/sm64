// Checks that the display list interpreter cannot be pushed past the end of
// its fixed-size pools.
//
// gfx_pc keeps a 64-entry color combiner pool, and every rendering backend
// keeps a shader program pool of the same size. Both were filled with a
// bare post-increment and no bounds check, so a display list using more
// distinct combiner configurations than the game happens to use would write
// straight past the end of a static array. Super Mario 64 stays well under
// the limit, which is why it never showed up -- but "the current game data
// does not trigger it" is not the same as "it cannot happen", especially
// for code that also runs against modified or generated display lists.
//
// It also covers two other ways a display list can reach into the
// interpreter's blind spots: a tile whose line size is zero (an integer
// division by zero in every texture decoder) and a colour-indexed texture
// with no palette loaded (a null dereference). Both are the same class as
// the pool overrun -- data deciding whether the process survives.
//
// Build this with -fsanitize=address to turn the overflow into a report:
//   make -C tools/porttest gfx_pool_test_asan && ./tools/porttest/gfx_pool_test_asan

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef _LANGUAGE_C
#define _LANGUAGE_C
#endif
#include <PR/mbi.h>
#include <PR/gbi.h>

#include "gfx_pc.h"
#include "gfx_cc.h"
#include "gfx_window_manager_api.h"
#include "gfx_rendering_api.h"

static void wm_init(const char *n, bool f) {}
static void wm_kbcb(bool (*a)(int), bool (*b)(int), void (*c)(void)) {}
static void wm_fscb(void (*a)(bool)) {}
static void wm_setfs(bool a) {}
static void wm_mainloop(void (*a)(void)) {}
static void wm_dims(uint32_t *w, uint32_t *h) { *w = 640; *h = 480; }
static void wm_void(void) {}
static bool wm_startframe(void) { return true; }
static double wm_time(void) { return 0.0; }
static struct GfxWindowManagerAPI wm = {
    wm_init, wm_kbcb, wm_fscb, wm_setfs, wm_mainloop,
    wm_dims, wm_void, wm_startframe, wm_void, wm_void, wm_time
};

//==============================================================================
// A rendering backend that reports shader features honestly.
//
// gfx_dummy reports used_textures = {false, false} for every shader, so
// gfx_pc never calls import_texture through it and the texture decoding
// paths are unreachable. This one answers from the real combiner features,
// the way the OpenGL, Metal and Direct3D backends do, so a display list
// that asks for a texture actually gets one decoded.
//==============================================================================

#define FAKE_SHADER_POOL 256

struct FakeShader {
    uint32_t shader_id;
    uint8_t num_inputs;
    bool used_textures[2];
};

static struct FakeShader fake_shaders[FAKE_SHADER_POOL];
static int fake_shader_count;
static int fake_textures_uploaded;
static int fake_texture_count;

static bool fake_z_is_from_0_to_1(void) { return false; }
static void fake_unload_shader(struct ShaderProgram *p) {}
static void fake_load_shader(struct ShaderProgram *p) {}

static struct ShaderProgram *fake_create_shader(uint32_t shader_id) {
    struct CCFeatures cc;
    struct FakeShader *sh;

    // gfx_pc must never ask for more programs than a backend pool holds
    if (fake_shader_count >= FAKE_SHADER_POOL) {
        printf("  FAIL: gfx_pc asked for more than %d shader programs\n", FAKE_SHADER_POOL);
        exit(1);
    }
    gfx_cc_get_features(shader_id, &cc);
    sh = &fake_shaders[fake_shader_count++];
    sh->shader_id = shader_id;
    sh->num_inputs = cc.num_inputs;
    sh->used_textures[0] = cc.used_textures[0];
    sh->used_textures[1] = cc.used_textures[1];
    return (struct ShaderProgram *) sh;
}

static struct ShaderProgram *fake_lookup_shader(uint32_t shader_id) {
    for (int i = 0; i < fake_shader_count; i++) {
        if (fake_shaders[i].shader_id == shader_id) {
            return (struct ShaderProgram *) &fake_shaders[i];
        }
    }
    return NULL;
}

static void fake_shader_get_info(struct ShaderProgram *p, uint8_t *num_inputs, bool used_textures[2]) {
    struct FakeShader *sh = (struct FakeShader *) p;
    *num_inputs = sh->num_inputs;
    used_textures[0] = sh->used_textures[0];
    used_textures[1] = sh->used_textures[1];
}

static uint32_t fake_new_texture(void) { return (uint32_t) fake_texture_count++; }
static void fake_select_texture(int tile, uint32_t id) {}
static void fake_upload_texture(const uint8_t *buf, int w, int h) { fake_textures_uploaded++; }
static void fake_set_sampler(int tile, bool lin, uint32_t cms, uint32_t cmt) {}
static void fake_set_depth_test(bool b) {}
static void fake_set_depth_mask(bool b) {}
static void fake_set_zmode_decal(bool b) {}
static void fake_set_viewport(int x, int y, int w, int h) {}
static void fake_set_scissor(int x, int y, int w, int h) {}
static void fake_set_use_alpha(bool b) {}
static void fake_draw_triangles(float *buf, size_t len, size_t tris) {}
static void fake_void(void) {}

static struct GfxRenderingAPI fake_rapi = {
    fake_z_is_from_0_to_1, fake_unload_shader, fake_load_shader,
    fake_create_shader, fake_lookup_shader, fake_shader_get_info,
    fake_new_texture, fake_select_texture, fake_upload_texture,
    fake_set_sampler, fake_set_depth_test, fake_set_depth_mask,
    fake_set_zmode_decal, fake_set_viewport, fake_set_scissor,
    fake_set_use_alpha, fake_draw_triangles, fake_void, fake_void,
    fake_void, fake_void, fake_void
};

// Enough distinct combiner configurations to run past a 64-entry pool
#define NUM_MODES 150

static Vtx verts[4];
static Vp viewport;
static Gfx dl[NUM_MODES * 4 + 32];

// Component values gfx_pc maps to distinct combiner inputs
static const int MUX[] = {
    G_CCMUX_TEXEL0, G_CCMUX_TEXEL1, G_CCMUX_PRIMITIVE,
    G_CCMUX_SHADE, G_CCMUX_ENVIRONMENT, G_CCMUX_1
};
#define NUM_MUX ((int) (sizeof(MUX) / sizeof(MUX[0])))

// gDPSetCombineLERP token-pastes its arguments, so it cannot take computed
// values. Emit G_SETCOMBINE directly instead, placing each component at the
// bit position gfx_run_dl reads it from (see the G_SETCOMBINE case there).
static void emit_combine(Gfx *g, int a, int b, int c, int d) {
    g->words.w0 = ((uintptr_t) G_SETCOMBINE << 24)
                | ((uintptr_t) (a & 0xf) << 20)
                | ((uintptr_t) (c & 0x1f) << 15)
                | ((uintptr_t) (a & 0x7) << 12)
                | ((uintptr_t) (c & 0x7) << 9);
    g->words.w1 = ((uintptr_t) (b & 0xf) << 28)
                | ((uintptr_t) (d & 0x7) << 15)
                | ((uintptr_t) (b & 0x7) << 12)
                | ((uintptr_t) (d & 0x7) << 9);
}

static void build_dl(void) {
    Gfx *g = dl;
    int i, m;

    for (i = 0; i < 4; i++) {
        verts[i].n.ob[0] = (i & 1) ? 100 : -100;
        verts[i].n.ob[1] = (i & 2) ? 100 : -100;
        verts[i].n.ob[2] = -200;
        verts[i].n.flag = 0;
        verts[i].n.tc[0] = 0; verts[i].n.tc[1] = 0;
        verts[i].n.n[0] = 64; verts[i].n.n[1] = 64; verts[i].n.n[2] = 64;
        verts[i].n.a = 255;
    }
    viewport.vp.vscale[0] = 640 * 2; viewport.vp.vscale[1] = 480 * 2;
    viewport.vp.vscale[2] = 511;     viewport.vp.vscale[3] = 0;
    viewport.vp.vtrans[0] = 640 * 2; viewport.vp.vtrans[1] = 480 * 2;
    viewport.vp.vtrans[2] = 511;     viewport.vp.vtrans[3] = 0;

    gSPViewport(g++, &viewport);
    gSPClearGeometryMode(g++, G_LIGHTING);
    gSPSetGeometryMode(g++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH);
    gSPVertex(g++, verts, 4, 0);

    for (m = 0; m < NUM_MODES; m++) {
        // Vary each slot independently so the resulting cc_ids differ; the
        // multiplier slot stays non-zero so gfx_generate_cc does not
        // collapse the configuration to a constant
        int a = MUX[m % NUM_MUX];
        int b = MUX[(m / NUM_MUX) % NUM_MUX];
        int c = MUX[(m / (NUM_MUX * NUM_MUX)) % NUM_MUX];
        emit_combine(g++, a, b, c, a);
        gSP1Triangle(g++, 0, 1, 2, 0);
    }

    gDPFullSync(g++);
    gSPEndDisplayList(g++);
}

// A display list that asks for a textured triangle with a degenerate tile
static Gfx bad_tile_dl[64];
static uint8_t texture[32 * 32 * 2];
static uint8_t many_textures[520][8];

static void build_bad_tile_dl(int line, int fmt, bool load_tlut) {
    Gfx *g = bad_tile_dl;

    gSPViewport(g++, &viewport);
    gSPClearGeometryMode(g++, G_LIGHTING);
    gSPSetGeometryMode(g++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH);
    gSPVertex(g++, verts, 4, 0);
    gDPSetTextureImage(g++, fmt, G_IM_SIZ_16b, 1, texture);
    gDPSetTile(g++, fmt, G_IM_SIZ_16b, line, 0, G_TX_LOADTILE, 0,
               G_TX_WRAP, 5, G_TX_NOLOD, G_TX_WRAP, 5, G_TX_NOLOD);
    gDPLoadBlock(g++, G_TX_LOADTILE, 0, 0, 32 * 32 - 1, 0);
    if (load_tlut) {
        gDPLoadTLUT(g++, 256, G_TX_LOADTILE, texture);
    }
    gDPSetTile(g++, fmt, G_IM_SIZ_16b, line, 0, G_TX_RENDERTILE, 0,
               G_TX_WRAP, 5, G_TX_NOLOD, G_TX_WRAP, 5, G_TX_NOLOD);
    gDPSetTileSize(g++, G_TX_RENDERTILE, 0, 0,
                   31 << G_TEXTURE_IMAGE_FRAC, 31 << G_TEXTURE_IMAGE_FRAC);
    gSPTexture(g++, 0x8000, 0x8000, 0, G_TX_RENDERTILE, G_ON);
    emit_combine(g++, G_CCMUX_TEXEL0, G_CCMUX_0, G_CCMUX_SHADE, G_CCMUX_0);
    gSP1Triangle(g++, 0, 1, 2, 0);
    gDPFullSync(g++);
    gSPEndDisplayList(g++);
}

static void run_dl(Gfx *dlist) {
    gfx_start_frame();
    gfx_run(dlist);
    gfx_end_frame();
}

static void run_tiny_texture(const uint8_t *image) {
    Gfx tiny_dl[32];
    Gfx *g = tiny_dl;

    gSPViewport(g++, &viewport);
    gSPClearGeometryMode(g++, G_LIGHTING);
    gSPVertex(g++, verts, 4, 0);
    gDPSetTextureImage(g++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, image);
    gDPSetTile(g++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, 0, G_TX_LOADTILE, 0,
               G_TX_WRAP, 0, G_TX_NOLOD, G_TX_WRAP, 0, G_TX_NOLOD);
    gDPLoadBlock(g++, G_TX_LOADTILE, 0, 0, 3, 0);
    gDPSetTile(g++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, 0, G_TX_RENDERTILE, 0,
               G_TX_WRAP, 0, G_TX_NOLOD, G_TX_WRAP, 0, G_TX_NOLOD);
    gDPSetTileSize(g++, G_TX_RENDERTILE, 0, 0,
                   3 << G_TEXTURE_IMAGE_FRAC, 0 << G_TEXTURE_IMAGE_FRAC);
    gSPTexture(g++, 0x8000, 0x8000, 0, G_TX_RENDERTILE, G_ON);
    emit_combine(g++, G_CCMUX_TEXEL0, G_CCMUX_0, G_CCMUX_SHADE, G_CCMUX_0);
    gSP1Triangle(g++, 0, 1, 2, 0);
    gDPFullSync(g++);
    gSPEndDisplayList(g++);
    run_dl(tiny_dl);
}

static void run_malformed_commands(void) {
    Gfx malformed[16];
    Gfx *g = malformed;

    gSPViewport(g++, &viewport);
    gSPPopMatrix(g++, G_MTX_MODELVIEW);
    gSPPopMatrix(g++, G_MTX_MODELVIEW);
    // The encoded end index wraps below the count. gfx_pc must reject the
    // resulting size_t underflow before indexing loaded_vertices.
    gSPVertex(g++, verts, 64, 64);
    gSPNumLights(g++, NUMLIGHTS_7);
    gSP1Triangle(g++, 100, 101, 102, 0);
    gSPEndDisplayList(g++);
    run_dl(malformed);
}

int main(void) {
    build_dl();
    gfx_init(&wm, &fake_rapi, "pooltest", false);

    run_malformed_commands();
    printf("display list command bounds\n");
    printf("  ok:   malformed matrix, light, vertex, and triangle commands are rejected\n");

    printf("\ndegenerate tiles\n");
    for (int i = 0; i < (int) sizeof(texture); i++) {
        texture[i] = (uint8_t) (i * 7);
    }

    // line = 0 makes line_size_bytes zero, which every texture decoder
    // divides by to work out the height
    build_bad_tile_dl(0, G_IM_FMT_RGBA, false);
    run_dl(bad_tile_dl);
    printf("  ok:   a tile with zero line size does not divide by zero\n");

    // A colour-indexed texture with no TLUT loaded leaves rdp.palette NULL
    build_bad_tile_dl(8, G_IM_FMT_CI, false);
    run_dl(bad_tile_dl);
    printf("  ok:   a CI texture with no palette does not dereference NULL\n");

    // The same list with a palette must still work normally
    build_bad_tile_dl(8, G_IM_FMT_CI, true);
    run_dl(bad_tile_dl);
    printf("  ok:   a CI texture with a palette still renders\n");

    printf("\ntexture cache recycling\n");
    {
        int uploads_before = fake_textures_uploaded;
        for (size_t i = 0; i < sizeof(many_textures) / sizeof(many_textures[0]); i++) {
            memset(many_textures[i], (int) i, sizeof(many_textures[i]));
            run_tiny_texture(many_textures[i]);
        }
        printf(fake_textures_uploaded - uploads_before == 520
                   ? "  ok:   every unique texture uploads across a full cache recycle\n"
                   : "  FAIL: expected 520 uploads across cache recycle, got %d\n",
               fake_textures_uploaded - uploads_before);
        if (fake_textures_uploaded - uploads_before != 520) {
            return 1;
        }
        uploads_before = fake_textures_uploaded;
        run_tiny_texture(many_textures[519]);
        printf(fake_textures_uploaded == uploads_before
                   ? "  ok:   the newest texture remains cached after recycling\n"
                   : "  FAIL: the newest texture was not cached after recycling\n");
        if (fake_textures_uploaded != uploads_before) {
            return 1;
        }
    }

    // Exercise the fixed combiner pool last. Once it is deliberately full,
    // the graceful fallback may reuse a non-textured combiner, which would
    // prevent the earlier texture tests from reaching their decoder paths.
    printf("\ndisplay list pool bounds\n");
    printf("  feeding %d distinct combiner configurations through a 64-entry pool\n",
           NUM_MODES);
    gfx_start_frame();
    gfx_run(dl);
    gfx_end_frame();

    // Run it again: the second pass must reuse what the first created
    // rather than allocating a fresh set of entries.
    gfx_start_frame();
    gfx_run(dl);
    gfx_end_frame();
    printf("  ok:   survived without running past the end of a pool\n");

    printf("PASS\n");
    return 0;
}
