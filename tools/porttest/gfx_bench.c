// Micro-benchmark for the N64 display list interpreter (src/pc/gfx/gfx_pc.c).
//
// Builds a synthetic display list of textured, lit triangles and runs it
// through gfx_pc against a rendering backend that does nothing, so what is
// measured is purely the interpreter's own CPU cost: vertex transform and
// lighting, state diffing, combiner and texture cache lookups, and filling
// the vertex buffer.
//
// This exists because "rendering a sub-frame re-runs the whole display
// list" sounds far more expensive than it measures. Use it before believing
// any claim about where this port spends its CPU time.
//
//   make -C tools/porttest && ./tools/porttest/gfx_bench [frames]
//
// Scene size is NUM_BATCHES * TRIS_PER_BATCH triangles; the default is in
// the range Super Mario 64 actually draws per frame.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
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

// The dummy window backend sleeps to pace 30 fps, which would drown out
// what we are trying to measure. This one does nothing at all.
static void wm_noop_init(const char *n, bool f) {}
static void wm_noop_kbcb(bool (*a)(int), bool (*b)(int), void (*c)(void)) {}
static void wm_noop_fscb(void (*a)(bool)) {}
static void wm_noop_setfs(bool a) {}
static void wm_noop_mainloop(void (*a)(void)) {}
static void wm_noop_dims(uint32_t *w, uint32_t *h) { *w = 1920; *h = 1080; }
static void wm_noop_void(void) {}
static bool wm_noop_startframe(void) { return true; }
static double wm_noop_time(void) { return 0.0; }
static struct GfxWindowManagerAPI wm_noop = {
    wm_noop_init, wm_noop_kbcb, wm_noop_fscb, wm_noop_setfs, wm_noop_mainloop,
    wm_noop_dims, wm_noop_void, wm_noop_startframe, wm_noop_void, wm_noop_void,
    wm_noop_time
};

#define NUM_BATCHES 100
#define TRIS_PER_BATCH 30

static Vtx verts[32];
static Gfx dl[NUM_BATCHES * (TRIS_PER_BATCH + 8) + 64];
static Vp viewport;
static uint8_t texture[32 * 32 * 2];
static Light_t lights[2];

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void build_dl(void) {
    Gfx *g = dl;
    int i, b, t;

    for (i = 0; i < 32; i++) {
        verts[i].n.ob[0] = (i * 137) % 400 - 200;
        verts[i].n.ob[1] = (i * 71) % 400 - 200;
        verts[i].n.ob[2] = (i * 53) % 400 - 200;
        verts[i].n.flag = 0;
        verts[i].n.tc[0] = (i * 31) % 1024;
        verts[i].n.tc[1] = (i * 17) % 1024;
        verts[i].n.n[0] = 60; verts[i].n.n[1] = 70; verts[i].n.n[2] = 80;
        verts[i].n.a = 255;
    }
    viewport.vp.vscale[0] = 320 * 2; viewport.vp.vscale[1] = 240 * 2;
    viewport.vp.vscale[2] = 511; viewport.vp.vscale[3] = 0;
    viewport.vp.vtrans[0] = 320 * 2; viewport.vp.vtrans[1] = 240 * 2;
    viewport.vp.vtrans[2] = 511; viewport.vp.vtrans[3] = 0;
    memset(lights, 0, sizeof(lights));
    lights[0].col[0] = lights[0].col[1] = lights[0].col[2] = 100;
    lights[0].dir[0] = 40; lights[0].dir[1] = 40; lights[0].dir[2] = 40;
    lights[1].col[0] = lights[1].col[1] = lights[1].col[2] = 60;

    gSPViewport(g++, &viewport);
    gSPSetGeometryMode(g++, G_ZBUFFER | G_SHADE | G_LIGHTING | G_SHADING_SMOOTH);
    gSPNumLights(g++, NUMLIGHTS_1);
    gSPLight(g++, &lights[0], 1);
    gSPLight(g++, &lights[1], 2);
    gDPSetTextureImage(g++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, texture);
    gDPSetTile(g++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 8, 0, G_TX_LOADTILE, 0,
               G_TX_WRAP, 5, G_TX_NOLOD, G_TX_WRAP, 5, G_TX_NOLOD);
    gDPLoadBlock(g++, G_TX_LOADTILE, 0, 0, 32 * 32 - 1, 0);
    gDPSetTile(g++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 8, 0, G_TX_RENDERTILE, 0,
               G_TX_WRAP, 5, G_TX_NOLOD, G_TX_WRAP, 5, G_TX_NOLOD);
    gDPSetTileSize(g++, G_TX_RENDERTILE, 0, 0, 31 << G_TEXTURE_IMAGE_FRAC, 31 << G_TEXTURE_IMAGE_FRAC);
    gSPTexture(g++, 0x8000, 0x8000, 0, G_TX_RENDERTILE, G_ON);

    for (b = 0; b < NUM_BATCHES; b++) {
        // Alternate combiner and render mode so state diffing and combiner
        // lookup are exercised the way a real scene exercises them
        if (b & 1) {
            gDPSetCombineMode(g++, G_CC_MODULATERGBA, G_CC_MODULATERGBA);
            gDPSetRenderMode(g++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
        } else {
            gDPSetCombineMode(g++, G_CC_MODULATERGB, G_CC_MODULATERGB);
            gDPSetRenderMode(g++, G_RM_AA_ZB_XLU_SURF, G_RM_AA_ZB_XLU_SURF2);
        }
        gSPVertex(g++, verts, 32, 0);
        for (t = 0; t < TRIS_PER_BATCH; t++) {
            gSP1Triangle(g++, t % 30, (t + 1) % 30, (t + 2) % 30, 0);
        }
    }
    gDPFullSync(g++);
    gSPEndDisplayList(g++);
}

int main(int argc, char **argv) {
    int reps = argc > 1 ? atoi(argv[1]) : 300;
    double t0, t1;
    int i;

    for (i = 0; i < (int) sizeof(texture); i++) {
        texture[i] = (uint8_t) (i * 7);
    }
    build_dl();
    gfx_init(&wm_noop, &gfx_dummy_renderer_api, "bench", false);

    // Warm the texture, combiner and shader caches
    for (i = 0; i < 5; i++) { gfx_start_frame(); gfx_run(dl); gfx_end_frame(); }

    t0 = now_sec();
    for (i = 0; i < reps; i++) { gfx_start_frame(); gfx_run(dl); gfx_end_frame(); }
    t1 = now_sec();

    printf("%d frames of %d tris: %.2f ms/frame\n",
           reps, NUM_BATCHES * TRIS_PER_BATCH, (t1 - t0) * 1000.0 / reps);
    return 0;
}
