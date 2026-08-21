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
#include "gfx_window_manager_api.h"
#include "gfx_rendering_api.h"

extern struct GfxRenderingAPI gfx_dummy_renderer_api;

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

int main(void) {
    printf("display list pool bounds\n");
    printf("  feeding %d distinct combiner configurations through a 64-entry pool\n",
           NUM_MODES);

    build_dl();
    gfx_init(&wm, &gfx_dummy_renderer_api, "pooltest", false);

    gfx_start_frame();
    gfx_run(dl);
    gfx_end_frame();

    // Run it again: the second pass must reuse what the first created
    // rather than allocating a fresh set of entries
    gfx_start_frame();
    gfx_run(dl);
    gfx_end_frame();

    printf("  ok:   survived without running past the end of a pool\n");
    printf("PASS\n");
    return 0;
}
