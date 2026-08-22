// Tests the adaptive on-screen controller in controller_touch.c.
//
// Layout coordinates are stored relative to UIKit's safe area and there are
// independent portrait and landscape profiles. These checks cover the
// persistence/editing behavior plus representative iPhone and iPad drawable
// geometries, including Dynamic Island and home-indicator margins.

#include <stdbool.h>
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

struct Geometry {
    const char *name;
    int width;
    int height;
    int left;
    int top;
    int right;
    int bottom;
    float scale;
};

static const struct Geometry iphone_landscape = {
    "Dynamic Island iPhone landscape", 2622, 1206, 177, 0, 177, 63, 3.0f
};
static const struct Geometry iphone_portrait = {
    "Dynamic Island iPhone portrait", 1206, 2622, 0, 177, 0, 102, 3.0f
};

static struct Geometry geometry;

static void use_geometry(const struct Geometry *value) {
    geometry = *value;
    touch_set_screen_geometry(value->width, value->height,
                              value->left, value->top,
                              value->right, value->bottom, value->scale);
}

static float safe_x(float x) {
    int width = geometry.width - geometry.left - geometry.right;
    return ((float) geometry.left + x * (float) width) / (float) geometry.width;
}

static float safe_y(float y) {
    int height = geometry.height - geometry.top - geometry.bottom;
    return ((float) geometry.top + y * (float) height) / (float) geometry.height;
}

static bool press_reads_a(float x, float y) {
    OSContPad pad;
    memset(&pad, 0, sizeof(pad));
    touch_down(1, x, y);
    controller_touch.read(&pad);
    touch_up(1);
    return (pad.button & A_BUTTON) != 0;
}

static bool overlay_fits_safe_area(void) {
    int count = 0;
    const float *vertices = touch_overlay_build(geometry.width, geometry.height, &count);
    float min_x = (float) geometry.left - 0.05f;
    float max_x = (float) (geometry.width - geometry.right) + 0.05f;
    float min_y = (float) geometry.top - 0.05f;
    float max_y = (float) (geometry.height - geometry.bottom) + 0.05f;

    if (count == 0) {
        return false;
    }
    for (int i = 0; i < count; i++) {
        float x = (vertices[i * 6] + 1.0f) * 0.5f * (float) geometry.width;
        float y = (1.0f - vertices[i * 6 + 1]) * 0.5f * (float) geometry.height;
        if (x < min_x || x > max_x || y < min_y || y > max_y) {
            return false;
        }
    }
    return true;
}

static void finish_edit(void) {
    touch_down(99, 0.01f, 0.01f);
    touch_up(99);
}

static void drag_a(float from_x, float from_y, float to_x, float to_y) {
    touch_layout_edit_set(true);
    touch_down(7, from_x, from_y);
    touch_motion(7, to_x, to_y);
    touch_up(7);
    finish_edit();
}

static void check_device_geometries(void) {
    static const struct Geometry devices[] = {
        { "compact iPhone landscape", 1334, 750, 0, 0, 0, 0, 2.0f },
        { "compact iPhone portrait", 750, 1334, 0, 40, 0, 0, 2.0f },
        { "Dynamic Island iPhone landscape", 2622, 1206, 177, 0, 177, 63, 3.0f },
        { "Dynamic Island iPhone portrait", 1206, 2622, 0, 177, 0, 102, 3.0f },
        { "13-inch iPad landscape", 2732, 2048, 0, 48, 0, 40, 2.0f },
        { "13-inch iPad portrait", 2048, 2732, 0, 48, 0, 40, 2.0f },
        { "13-inch iPad portrait retro drawable", 180, 240, 0, 4, 0, 4, 0.174f },
    };

    for (size_t i = 0; i < sizeof(devices) / sizeof(devices[0]); i++) {
        char message[128];
        use_geometry(&devices[i]);
        touch_layout_reset();
        snprintf(message, sizeof(message), "%s controls stay inside its safe area", devices[i].name);
        CHECK(overlay_fits_safe_area(), message);
    }

    // The player's scale setting is applied after layout. Even an unusually
    // large value must not push the visible circles under unsafe screen edges.
    use_geometry(&iphone_portrait);
    touch_layout_reset();
    configTouchScale = 3.0f;
    CHECK(overlay_fits_safe_area(), "enlarged controls remain inside the portrait safe area");
    configTouchScale = 1.0f;
}

int main(void) {
    const char *layout = "sm64_touch_layout.txt";
    const float landscape_a_x = 0.900f;
    const float landscape_a_y = 0.720f;
    const float portrait_a_x = 0.830f;
    const float portrait_a_y = 0.820f;

    remove(layout);
    printf("adaptive on-screen control layout\n");
    configTouchScale = 1.0f;
    configTouchOpacity = 1.0f;

    use_geometry(&iphone_landscape);
    controller_touch.init();
    CHECK(press_reads_a(safe_x(landscape_a_x), safe_y(landscape_a_y)),
          "the landscape A button responds where it is drawn");
    CHECK(!press_reads_a(safe_x(0.2f), safe_y(0.5f)),
          "empty landscape space is not the A button");
    CHECK(overlay_fits_safe_area(), "landscape controls avoid the island and home indicator");

    // Size setting: a larger multiplier must widen the hit area in physical
    // pixels even though the authored coordinates are safe-area normalized.
    {
        float a_x = safe_x(landscape_a_x);
        float a_y = safe_y(landscape_a_y);
        float far_x = a_x - 125.0f / (float) geometry.width;
        configTouchScale = 0.8f;
        bool small_far = press_reads_a(far_x, a_y);
        configTouchScale = 1.45f;
        bool large_far = press_reads_a(far_x, a_y);
        configTouchScale = 1.0f;
        CHECK(!small_far && large_far, "the size setting scales the hit area");
    }

    // Enabling edit mode is itself reached through A. The finger that caused
    // that transition must not immediately count as the tap that exits it.
    touch_down(9, safe_x(landscape_a_x), safe_y(landscape_a_y));
    touch_layout_edit_set(true);
    touch_up(9);
    CHECK(touch_layout_edit_active(), "the finger that enabled edit mode does not cancel it");
    touch_layout_edit_set(false);

    // Customize landscape, rotate, then customize portrait. Each profile
    // should retain its own position both in memory and after a file reload.
    {
        float landscape_custom_x = safe_x(0.30f);
        float landscape_custom_y = safe_y(0.40f);
        drag_a(safe_x(landscape_a_x), safe_y(landscape_a_y),
               landscape_custom_x, landscape_custom_y);
        CHECK(!touch_layout_edit_active(), "tapping empty space leaves edit mode");
        CHECK(press_reads_a(landscape_custom_x, landscape_custom_y),
              "the landscape button moved to its edited position");

        use_geometry(&iphone_portrait);
        CHECK(press_reads_a(safe_x(portrait_a_x), safe_y(portrait_a_y)),
              "portrait starts from its independent authored layout");
        CHECK(overlay_fits_safe_area(), "portrait controls avoid the island and home indicator");
        CHECK(!press_reads_a(safe_x(portrait_a_x) - 115.0f / (float) geometry.width,
                             safe_y(portrait_a_y)),
              "portrait phone controls use the compact touch radius");

        float portrait_custom_x = safe_x(0.36f);
        float portrait_custom_y = safe_y(0.68f);
        drag_a(safe_x(portrait_a_x), safe_y(portrait_a_y),
               portrait_custom_x, portrait_custom_y);
        CHECK(press_reads_a(portrait_custom_x, portrait_custom_y),
              "the portrait button moved independently");

        use_geometry(&iphone_landscape);
        CHECK(press_reads_a(safe_x(0.30f), safe_y(0.40f)),
              "rotating back restores the landscape edit");

        controller_touch.init();
        CHECK(press_reads_a(safe_x(0.30f), safe_y(0.40f)),
              "the landscape profile survives a reload");
        use_geometry(&iphone_portrait);
        CHECK(press_reads_a(safe_x(0.36f), safe_y(0.68f)),
              "the portrait profile survives the same reload");
    }

    // A resize/rotation must cancel active input. Otherwise a finger recorded
    // against the previous coordinate system can leave a button held forever.
    use_geometry(&iphone_landscape);
    touch_layout_reset();
    touch_down(12, safe_x(landscape_a_x), safe_y(landscape_a_y));
    {
        OSContPad pad;
        memset(&pad, 0, sizeof(pad));
        controller_touch.read(&pad);
        CHECK((pad.button & A_BUTTON) != 0, "A is held before rotation");
        use_geometry(&iphone_portrait);
        memset(&pad, 0, sizeof(pad));
        controller_touch.read(&pad);
        CHECK((pad.button & A_BUTTON) == 0, "rotation cancels active touch input");
    }

    check_device_geometries();

    // Version 1 files had no profile prefix. They must still restore into
    // landscape without corrupting the new portrait layout.
    {
        FILE *f = fopen(layout, "wb");
        fprintf(f, "a 0.2500 0.5000 0.0850\n");
        fclose(f);
    }
    use_geometry(&iphone_landscape);
    controller_touch.init();
    CHECK(press_reads_a(safe_x(0.25f), safe_y(0.50f)),
          "a legacy layout restores into landscape");
    use_geometry(&iphone_portrait);
    CHECK(press_reads_a(safe_x(portrait_a_x), safe_y(portrait_a_y)),
          "a legacy landscape file leaves portrait at its default");

    // Damaged coordinates should be ignored rather than marooning a control.
    {
        FILE *f = fopen(layout, "wb");
        fprintf(f, "portrait a 47.0 -3.0 900.0\nbogus line\ncleft 0.5\n");
        fclose(f);
    }
    controller_touch.init();
    CHECK(press_reads_a(safe_x(portrait_a_x), safe_y(portrait_a_y)),
          "out-of-range portrait values fall back to the default");

    remove(layout);
    remove("sm64_touch_layout.txt.tmp");
    printf(failures == 0 ? "PASS\n" : "FAILED (%d)\n", failures);
    return failures != 0;
}
