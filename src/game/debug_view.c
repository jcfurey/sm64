// Collision surface visualizer: draws the collision mesh around Mario as
// translucent colored triangles on top of the rendered scene.

#ifndef TARGET_N64

#include <PR/ultratypes.h>
#include <PR/gbi.h>

#include "sm64.h"
#include "game_init.h"
#include "memory.h"
#include "object_list_processor.h"
#include "engine/surface_load.h"
#include "engine/surface_collision.h"
#include "debug_view.h"

#include "pc/configfile.h"

// Draw distance from Mario and the triangle budget per frame
#define COLLISION_VIEW_RADIUS 2000.0f
#define COLLISION_VIEW_MAX_TRIS 512

// Emitted geometry per triangle: one gSPVertex and one gSP1Triangle
#define GFX_PER_TRI 2
#define GFX_OVERHEAD 12

static Vtx *sVtxCursor;
static Gfx *sGfxCursor;
static s32 sTriCount;

static void collision_view_add_surface(struct Surface *surf, f32 x, f32 z) {
    Vtx *v = sVtxCursor;
    u8 r, g, b;
    s32 i;

    if (sTriCount >= COLLISION_VIEW_MAX_TRIS) {
        return;
    }

    // Cheap lateral distance cull on the first vertex
    {
        f32 dx = surf->vertex1[0] - x;
        f32 dz = surf->vertex1[2] - z;
        if (dx > COLLISION_VIEW_RADIUS || dx < -COLLISION_VIEW_RADIUS
            || dz > COLLISION_VIEW_RADIUS || dz < -COLLISION_VIEW_RADIUS) {
            return;
        }
    }

    if (surf->normal.y > 0.01f) {
        r = 40; g = 220; b = 60;   // floor
    } else if (surf->normal.y < -0.01f) {
        r = 230; g = 60; b = 40;   // ceiling
    } else {
        r = 60; g = 120; b = 230;  // wall
    }

    for (i = 0; i < 3; i++) {
        s16 *vertex = i == 0 ? surf->vertex1 : (i == 1 ? surf->vertex2 : surf->vertex3);
        v[i].v.ob[0] = vertex[0];
        v[i].v.ob[1] = vertex[1];
        v[i].v.ob[2] = vertex[2];
        v[i].v.flag = 0;
        v[i].v.tc[0] = 0;
        v[i].v.tc[1] = 0;
        v[i].v.cn[0] = r;
        v[i].v.cn[1] = g;
        v[i].v.cn[2] = b;
        v[i].v.cn[3] = 120;
    }

    gSPVertex(sGfxCursor++, VIRTUAL_TO_PHYSICAL(v), 3, 0);
    gSP1Triangle(sGfxCursor++, 0, 1, 2, 0);

    sVtxCursor += 3;
    sTriCount++;
}

static void collision_view_walk_partition(SpatialPartitionCell (*partition)[NUM_CELLS], f32 x, f32 z) {
    s32 cellX, cellZ, list;

    for (cellZ = 0; cellZ < NUM_CELLS; cellZ++) {
        for (cellX = 0; cellX < NUM_CELLS; cellX++) {
            for (list = 0; list < 3; list++) {
                struct SurfaceNode *node = partition[cellZ][cellX][list].next;
                while (node != NULL) {
                    collision_view_add_surface(node->surface, x, z);
                    node = node->next;
                }
            }
        }
    }
}

void debug_view_append_collision(void) {
    Gfx *dlStart;
    Vtx *vtxStart;
    f32 x, z;

    if (configViewMode != 2 || gMarioObject == NULL) {
        return;
    }

    dlStart = alloc_display_list((COLLISION_VIEW_MAX_TRIS * GFX_PER_TRI + GFX_OVERHEAD) * sizeof(Gfx));
    vtxStart = alloc_display_list(COLLISION_VIEW_MAX_TRIS * 3 * sizeof(Vtx));
    if (dlStart == NULL || vtxStart == NULL) {
        return;
    }

    x = gMarioObject->header.gfx.pos[0];
    z = gMarioObject->header.gfx.pos[2];

    sGfxCursor = dlStart;
    sVtxCursor = vtxStart;
    sTriCount = 0;

    gDPPipeSync(sGfxCursor++);
    gDPSetCombineMode(sGfxCursor++, G_CC_SHADE, G_CC_SHADE);
    gDPSetRenderMode(sGfxCursor++, G_RM_AA_ZB_XLU_DECAL, G_RM_AA_ZB_XLU_DECAL2);
    gSPClearGeometryMode(sGfxCursor++, G_LIGHTING | G_CULL_BACK);
    gSPTexture(sGfxCursor++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF);

    collision_view_walk_partition(gStaticSurfacePartition, x, z);
    collision_view_walk_partition(gDynamicSurfacePartition, x, z);

    gDPPipeSync(sGfxCursor++);
    gSPSetGeometryMode(sGfxCursor++, G_LIGHTING | G_CULL_BACK);
    gSPEndDisplayList(sGfxCursor++);

    if (sTriCount > 0) {
        extern void geo_append_debug_display_list(void *displayList, s16 layer);
        geo_append_debug_display_list((void *) VIRTUAL_TO_PHYSICAL(dlStart), 6);
    }
}

#endif
