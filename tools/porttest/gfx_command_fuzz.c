// Structured libFuzzer harness for bounds-sensitive display-list commands.
// Pointers always refer to local valid storage; the fuzzer controls command
// counts, indices, viewport values, light counts, and matrix parameters.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef _LANGUAGE_C
#define _LANGUAGE_C
#endif
#include <PR/mbi.h>
#include <PR/gbi.h>

#include "gfx_dummy.h"
#include "gfx_pc.h"

static uint8_t next_byte(const uint8_t *data, size_t size, size_t *position) {
    if (*position >= size) {
        return 0;
    }
    return data[(*position)++];
}

static uint16_t next_u16(const uint8_t *data, size_t size, size_t *position) {
    uint16_t high = next_byte(data, size, position);
    return (uint16_t) ((high << 8) | next_byte(data, size, position));
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    static bool initialized;
    Vtx vertices[64];
    Mtx matrix;
    Vp viewport;
    Gfx commands[8];
    Gfx *g = commands;
    size_t position = 0;
    uint8_t vertex_count;
    uint8_t vertex_end;

    if (!initialized) {
        gfx_init(&gfx_dummy_wm_api, &gfx_dummy_renderer_api, "gfx-fuzz", false);
        initialized = true;
    }

    memset(vertices, 0, sizeof(vertices));
    memset(&matrix, 0, sizeof(matrix));
    memset(&viewport, 0, sizeof(viewport));
    viewport.vp.vscale[0] = (int16_t) next_u16(data, size, &position);
    viewport.vp.vscale[1] = (int16_t) next_u16(data, size, &position);
    viewport.vp.vtrans[0] = (int16_t) next_u16(data, size, &position);
    viewport.vp.vtrans[1] = (int16_t) next_u16(data, size, &position);
    gSPViewport(g++, &viewport);

    g->words.w0 = ((uintptr_t) G_MTX << 24) | next_byte(data, size, &position);
    g->words.w1 = (uintptr_t) &matrix;
    g++;

    g->words.w0 = (uintptr_t) G_POPMTX << 24;
    g->words.w1 = (uintptr_t) next_byte(data, size, &position) * 64;
    g++;

    g->words.w0 = ((uintptr_t) G_MOVEWORD << 24)
                | ((uintptr_t) G_MW_NUMLIGHT << 16);
    g->words.w1 = next_u16(data, size, &position) * 24U;
    g++;

    vertex_count = next_byte(data, size, &position);
    vertex_end = next_byte(data, size, &position);
    g->words.w0 = ((uintptr_t) G_VTX << 24)
                | ((uintptr_t) vertex_count << 12)
                | ((uintptr_t) (vertex_end & 0x7f) << 1);
    g->words.w1 = (uintptr_t) vertices;
    g++;

    g->words.w0 = ((uintptr_t) G_TRI1 << 24)
                | ((uintptr_t) next_byte(data, size, &position) << 16)
                | ((uintptr_t) next_byte(data, size, &position) << 8)
                | next_byte(data, size, &position);
    g->words.w1 = 0;
    g++;

    gSPEndDisplayList(g++);

    gfx_start_frame();
    gfx_run(commands);
    gfx_end_frame();
    return 0;
}

#ifdef SM64_STANDALONE_FUZZ
int main(int argc, char **argv) {
    unsigned int iterations = argc > 1 ? (unsigned int) strtoul(argv[1], NULL, 10) : 10000;
    uint32_t state = 0x534d3634U;
    uint8_t input[32];

    for (unsigned int run = 0; run < iterations; run++) {
        for (size_t i = 0; i < sizeof(input); i++) {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            input[i] = (uint8_t) state;
        }
        LLVMFuzzerTestOneInput(input, sizeof(input));
    }
    return 0;
}
#endif
