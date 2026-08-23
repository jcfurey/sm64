#include <stdio.h>

#include "gfx/gfx_viewport.h"

static int failures;

#define CHECK(cond, what)                                                     \
    do {                                                                      \
        if (!(cond)) {                                                        \
            printf("  FAIL: %s\n", what);                                   \
            failures++;                                                       \
        } else {                                                              \
            printf("  ok:   %s\n", what);                                   \
        }                                                                     \
    } while (0)

int main(void) {
    struct GfxViewportLayout layout;

    printf("game viewport layout\n");

    gfx_viewport_layout_calculate(2622, 1206, 177, 0, 177, 63,
                                  true, false, &layout);
    CHECK(layout.x == 0 && layout.y == 0
          && layout.width == 2622 && layout.height == 1206,
          "landscape remains full-screen");
    CHECK(layout.safe_left == 177 && layout.safe_right == 177
          && layout.safe_top == 0 && layout.safe_bottom == 63,
          "landscape publishes unsafe HUD margins without shrinking the scene");

    gfx_viewport_layout_calculate(1206, 2622, 0, 177, 0, 102,
                                  true, false, &layout);
    CHECK(layout.x == 0 && layout.y == 1541
          && layout.width == 1206 && layout.height == 904,
          "portrait fits a top-aligned 4:3 game panel below the island");
    CHECK(layout.width * 3 == layout.height * 4 + 2,
          "integer rounding keeps the portrait panel at 4:3");
    CHECK(layout.safe_left == 0 && layout.safe_top == 0
          && layout.safe_right == 0 && layout.safe_bottom == 0,
          "a portrait panel fitted inside the safe area needs no HUD margins");

    gfx_viewport_layout_calculate(2048, 2732, 0, 48, 0, 40,
                                  true, false, &layout);
    CHECK(layout.y == 1148 && layout.width == 2048 && layout.height == 1536,
          "portrait iPad leaves a lower control deck");

    gfx_viewport_layout_calculate(2732, 2048, 0, 48, 0, 40,
                                  true, true, &layout);
    CHECK(layout.x == 1 && layout.y == 0
          && layout.width == 2730 && layout.height == 2048,
          "retro landscape keeps its centered 4:3 pillarbox");

    gfx_viewport_layout_calculate(0, 0, 99, 99, 99, 99,
                                  true, false, &layout);
    CHECK(layout.width == 1 && layout.height == 1,
          "invalid startup geometry has a safe fallback");

    printf(failures == 0 ? "PASS\n" : "FAILED (%d)\n", failures);
    return failures != 0;
}
