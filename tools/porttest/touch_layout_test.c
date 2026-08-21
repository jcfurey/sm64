// Tests the on-screen control layout in src/pc/controller/controller_touch.c.
//
// The fixed button positions are the most common complaint about any mobile
// port, so the layout is adjustable and persisted. What is checked here is
// that hit testing tracks the size setting, that dragging keeps buttons on
// screen, and that a saved layout survives a round trip -- including a
// damaged file, which must degrade to the default rather than place buttons
// somewhere unreachable.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "controller/controller_touch.h"
#include "configfile.h"

static int failures;

#define CHECK(cond, what)                                                     \
    do {                                                                      \
        if (!(cond)) {                                                        \
            printf("  FAIL: %s\n", what);                                     \
            failures++;                                                       \
        } else {                                                              \
            printf("  ok:   %s\n", what);                                     \
        }                                                                     \
    } while (0)

// The A button, at its authored position
#define A_X 0.905f
#define A_Y 0.720f

// Presses at (x, y) and reports whether the pad saw the A button
static bool press_reads_a(float x, float y) {
    OSContPad pad;
    memset(&pad, 0, sizeof(pad));
    touch_down(1, x, y);
    controller_touch.read(&pad);
    touch_up(1);
    return (pad.button & A_BUTTON) != 0;
}

int main(void) {
    const char *layout = "sm64_touch_layout.txt";

    remove(layout);
    printf("on-screen control layout\n");

    touch_set_screen_size(1920, 1080);
    controller_touch.init();

    CHECK(press_reads_a(A_X, A_Y), "the A button responds where it is drawn");
    CHECK(!press_reads_a(0.2f, 0.5f), "empty space is not the A button");

    // Size setting: a larger multiplier must widen the hit area
    configTouchScale = 0.8f;
    bool smallFar = press_reads_a(A_X - 0.075f, A_Y);
    configTouchScale = 1.45f;
    bool largeFar = press_reads_a(A_X - 0.075f, A_Y);
    configTouchScale = 1.0f;
    CHECK(!smallFar && largeFar, "the size setting scales the hit area");

    // Edit mode is switched on by pressing the on-screen A button, so the
    // finger that turned it on is still down when it starts. Its lift must
    // not be mistaken for the empty-space tap that ends editing, or the
    // feature is unreachable by touch -- which is the only input an iOS
    // player has.
    touch_down(9, A_X, A_Y);
    touch_layout_edit_set(true);
    touch_up(9);
    CHECK(touch_layout_edit_active(), "the finger that enabled edit mode does not cancel it");
    touch_layout_edit_set(false);

    // Dragging in edit mode moves a button and does not press it
    touch_layout_edit_set(true);
    CHECK(touch_layout_edit_active(), "edit mode turns on");

    // A second touch landing on empty space (a resting palm) must not
    // cancel a drag that another finger is in the middle of
    touch_down(7, A_X, A_Y);
    touch_motion(7, 0.50f, 0.50f);
    touch_down(8, 0.10f, 0.90f);
    touch_up(8);
    CHECK(touch_layout_edit_active(), "a stray second touch does not end a drag in progress");
    touch_up(7);
    {
        OSContPad pad;
        memset(&pad, 0, sizeof(pad));
        touch_down(2, 0.50f, 0.50f);
        touch_motion(2, 0.30f, 0.40f);
        controller_touch.read(&pad);
        CHECK(pad.button == 0, "dragging a button does not press it");
        touch_up(2);
    }

    // Lifting from empty space finishes editing and writes the layout
    touch_down(4, 0.60f, 0.60f);
    touch_up(4);
    CHECK(!touch_layout_edit_active(), "tapping empty space leaves edit mode");
    {
        FILE *f = fopen(layout, "rb");
        CHECK(f != NULL, "the layout was saved");
        if (f != NULL) {
            fclose(f);
        }
    }

    CHECK(press_reads_a(0.30f, 0.40f), "the button moved to where it was dragged");
    CHECK(!press_reads_a(A_X, A_Y), "and is no longer at the old spot");

    // Reloading restores the customized position rather than the default
    controller_touch.init();
    CHECK(press_reads_a(0.30f, 0.40f), "the saved position survives a reload");

    // A drag heading off screen has to stop at the edge, or the button
    // becomes impossible to reach again
    touch_layout_edit_set(true);
    touch_down(3, 0.30f, 0.40f);
    touch_motion(3, 5.0f, -5.0f);
    touch_up(3);
    touch_down(5, 0.50f, 0.50f);
    touch_up(5);
    CHECK(!press_reads_a(0.30f, 0.40f), "a far drag moves the button");
    CHECK(!press_reads_a(2.0f, -1.0f), "the button did not follow the finger off screen");
    {
        // It must have come to rest inside the clamped region
        bool found = false;
        float x, y;
        for (y = 0.0f; y <= 1.0f && !found; y += 0.02f) {
            for (x = 0.0f; x <= 1.0f && !found; x += 0.02f) {
                found = press_reads_a(x, y);
            }
        }
        CHECK(found, "a drag off screen is clamped back into view");
    }

    // A damaged file must not place buttons at nonsense coordinates
    {
        FILE *f = fopen(layout, "wb");
        fprintf(f, "a 47.0 -3.0 900.0\nbogus line\ncleft 0.5\n");
        fclose(f);
    }
    controller_touch.init();
    CHECK(press_reads_a(A_X, A_Y), "out-of-range values fall back to the default");

    touch_layout_reset();
    CHECK(press_reads_a(A_X, A_Y), "reset restores the authored layout");

    remove(layout);
    remove("sm64_touch_layout.txt.tmp");
    printf(failures == 0 ? "PASS\n" : "FAILED (%d)\n", failures);
    return failures != 0;
}
