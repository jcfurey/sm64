// Touch screen controller and on-screen control overlay for iOS.
//
// The left part of the screen acts as a floating analog stick: the spot
// where a finger lands becomes the stick's neutral position and dragging
// away from it deflects the stick. The right part of the screen carries
// fixed zones for the N64 buttons, drawn by touch_render_overlay() as a
// semi-transparent overlay after each game frame.

#ifdef TARGET_IOS

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <SDL2/SDL.h>

#ifdef ENABLE_OPENGL
#define GL_GLEXT_PROTOTYPES 1
#include <SDL2/SDL_opengles2.h>
#endif

#include <ultra64.h>

#include "controller_api.h"
#include "controller_gamepad.h"
#include "controller_touch.h"
#include "../configfile.h"
#include "../fs.h"

#define MAX_FINGERS 10

// Full stick deflection distance, as a fraction of the safe area's shorter
// side. Using the shorter side keeps controls at a useful physical scale in
// both portrait and landscape.
#define STICK_RANGE 0.12f
// Fingers landing in this part of the safe area control the stick.
#define STICK_ZONE_X 0.45f
#define STICK_ZONE_Y 0.18f

enum TouchButtonId {
    TOUCH_A,
    TOUCH_B,
    TOUCH_Z,
    TOUCH_R,
    TOUCH_START,
    TOUCH_C_UP,
    TOUCH_C_DOWN,
    TOUCH_C_LEFT,
    TOUCH_C_RIGHT,
    TOUCH_BUTTON_COUNT
};

struct TouchButton {
    float cx; // center, fraction of safe-area width
    float cy; // center, fraction of safe-area height
    float r;  // radius, fraction of the safe area's shorter side
    uint16_t mask;
    float color[4];
};

enum TouchLayoutProfile {
    TOUCH_LAYOUT_LANDSCAPE,
    TOUCH_LAYOUT_PORTRAIT,
    TOUCH_LAYOUT_PROFILE_COUNT
};

// Colors follow the N64 pad: A blue, B green, C yellow, Start red.
// Positions are normalized within UIKit's safe area, not a particular device
// model. That makes one pair of layouts cover every iPhone and iPad size.
static const struct TouchButton touch_button_defaults[TOUCH_LAYOUT_PROFILE_COUNT][TOUCH_BUTTON_COUNT] = {
    [TOUCH_LAYOUT_LANDSCAPE] = {
        [TOUCH_A]       = { 0.900f, 0.720f, 0.085f, A_BUTTON,     { 0.25f, 0.35f, 0.95f, 1.0f } },
        [TOUCH_B]       = { 0.775f, 0.860f, 0.065f, B_BUTTON,     { 0.20f, 0.80f, 0.30f, 1.0f } },
        [TOUCH_Z]       = { 0.655f, 0.700f, 0.055f, Z_TRIG,       { 0.60f, 0.60f, 0.65f, 1.0f } },
        [TOUCH_R]       = { 0.940f, 0.095f, 0.050f, R_TRIG,       { 0.60f, 0.60f, 0.65f, 1.0f } },
        [TOUCH_START]   = { 0.500f, 0.085f, 0.050f, START_BUTTON, { 0.90f, 0.25f, 0.25f, 1.0f } },
        [TOUCH_C_UP]    = { 0.855f, 0.330f, 0.042f, U_CBUTTONS,   { 0.95f, 0.80f, 0.15f, 1.0f } },
        [TOUCH_C_DOWN]  = { 0.855f, 0.510f, 0.042f, D_CBUTTONS,   { 0.95f, 0.80f, 0.15f, 1.0f } },
        [TOUCH_C_LEFT]  = { 0.767f, 0.420f, 0.042f, L_CBUTTONS,   { 0.95f, 0.80f, 0.15f, 1.0f } },
        [TOUCH_C_RIGHT] = { 0.943f, 0.420f, 0.042f, R_CBUTTONS,   { 0.95f, 0.80f, 0.15f, 1.0f } },
    },
    [TOUCH_LAYOUT_PORTRAIT] = {
        [TOUCH_A]       = { 0.830f, 0.820f, 0.085f, A_BUTTON,     { 0.25f, 0.35f, 0.95f, 1.0f } },
        [TOUCH_B]       = { 0.670f, 0.900f, 0.065f, B_BUTTON,     { 0.20f, 0.80f, 0.30f, 1.0f } },
        [TOUCH_Z]       = { 0.540f, 0.780f, 0.055f, Z_TRIG,       { 0.60f, 0.60f, 0.65f, 1.0f } },
        [TOUCH_R]       = { 0.880f, 0.440f, 0.050f, R_TRIG,       { 0.60f, 0.60f, 0.65f, 1.0f } },
        [TOUCH_START]   = { 0.500f, 0.460f, 0.050f, START_BUTTON, { 0.90f, 0.25f, 0.25f, 1.0f } },
        [TOUCH_C_UP]    = { 0.790f, 0.570f, 0.042f, U_CBUTTONS,   { 0.95f, 0.80f, 0.15f, 1.0f } },
        [TOUCH_C_DOWN]  = { 0.790f, 0.690f, 0.042f, D_CBUTTONS,   { 0.95f, 0.80f, 0.15f, 1.0f } },
        [TOUCH_C_LEFT]  = { 0.700f, 0.630f, 0.042f, L_CBUTTONS,   { 0.95f, 0.80f, 0.15f, 1.0f } },
        [TOUCH_C_RIGHT] = { 0.880f, 0.630f, 0.042f, R_CBUTTONS,   { 0.95f, 0.80f, 0.15f, 1.0f } },
    },
};

static struct TouchButton touch_buttons[TOUCH_LAYOUT_PROFILE_COUNT][TOUCH_BUTTON_COUNT];

static const char *const touch_profile_names[TOUCH_LAYOUT_PROFILE_COUNT] = {
    [TOUCH_LAYOUT_LANDSCAPE] = "landscape",
    [TOUCH_LAYOUT_PORTRAIT] = "portrait",
};

// Short stable names, used as keys in the saved layout file
static const char *const touch_button_names[TOUCH_BUTTON_COUNT] = {
    [TOUCH_A] = "a",           [TOUCH_B] = "b",
    [TOUCH_Z] = "z",           [TOUCH_R] = "r",
    [TOUCH_START] = "start",   [TOUCH_C_UP] = "cup",
    [TOUCH_C_DOWN] = "cdown",  [TOUCH_C_LEFT] = "cleft",
    [TOUCH_C_RIGHT] = "cright",
};

enum FingerRole {
    ROLE_NONE,
    ROLE_STICK,
    ROLE_BUTTON,
    // Layout edit mode. The role is decided when the finger lands, so a
    // finger that was already down when edit mode was switched on -- the
    // one that pressed A to switch it on -- is neither a drag nor the tap
    // that ends editing.
    ROLE_LAYOUT_DRAG,
    ROLE_LAYOUT_TAP
};

struct Finger {
    bool active;
    long long id;
    enum FingerRole role;
    int button; // valid when role is ROLE_BUTTON or ROLE_LAYOUT_DRAG
    bool moved; // this finger has dragged something
    float x, y; // normalized position
    float origin_x, origin_y; // stick neutral position (normalized)
};

static struct Finger fingers[MAX_FINGERS];

static int screen_width = 1;
static int screen_height = 1;
static int safe_left;
static int safe_top;
static int safe_right;
static int safe_bottom;
static float screen_pixels_per_point = 1.0f;
static enum TouchLayoutProfile touch_profile = TOUCH_LAYOUT_LANDSCAPE;
static void (*haptic_callback)(void);

static int safe_width(void) {
    return screen_width - safe_left - safe_right;
}

static int safe_height(void) {
    return screen_height - safe_top - safe_bottom;
}

static float layout_unit(void) {
    int w = safe_width();
    int h = safe_height();
    float unit = (float) (w < h ? w : h);
    // A screen fraction feels consistent across phones, but grows excessive
    // on a 13-inch iPad. Cap the authored unit in UIKit points so tablets get
    // a comfortable modest increase while remaining independent of Retina
    // scale and exact device model.
    float point_cap = 560.0f * screen_pixels_per_point;
    return unit < point_cap ? unit : point_cap;
}

static float control_unit(void) {
    float unit = layout_unit();
    float safe_width_points = (float) safe_width() / screen_pixels_per_point;

    // Portrait phones have less visible game area around the authored
    // controls. Leave landscape and tablet sizing unchanged, while making
    // portrait phone controls compact enough to keep the action visible.
    if (touch_profile == TOUCH_LAYOUT_PORTRAIT && safe_width_points < 600.0f) {
        unit *= 0.72f;
    }
    return unit;
}

static struct TouchButton *current_buttons(void) {
    return touch_buttons[touch_profile];
}

void touch_set_screen_geometry(int width, int height,
                               int inset_left, int inset_top,
                               int inset_right, int inset_bottom,
                               float pixels_per_point) {
    int old_width = screen_width;
    int old_height = screen_height;
    int old_left = safe_left;
    int old_top = safe_top;
    int old_right = safe_right;
    int old_bottom = safe_bottom;
    float old_pixels_per_point = screen_pixels_per_point;
    enum TouchLayoutProfile old_profile = touch_profile;

    screen_width = width > 0 ? width : 1;
    screen_height = height > 0 ? height : 1;
    safe_left = inset_left > 0 ? inset_left : 0;
    safe_top = inset_top > 0 ? inset_top : 0;
    safe_right = inset_right > 0 ? inset_right : 0;
    safe_bottom = inset_bottom > 0 ? inset_bottom : 0;
    // Retro mode intentionally uses fewer than one drawable pixel per UIKit
    // point, so sub-1.0 scales are valid here.
    screen_pixels_per_point = pixels_per_point > 0.01f && pixels_per_point < 10.0f
        ? pixels_per_point : 1.0f;

    // Treat invalid or momentarily incomplete UIKit geometry as no insets.
    // This can occur during the first layout pass but must never produce a
    // negative coordinate space or divide by zero.
    if (safe_left + safe_right >= screen_width) {
        safe_left = 0;
        safe_right = 0;
    }
    if (safe_top + safe_bottom >= screen_height) {
        safe_top = 0;
        safe_bottom = 0;
    }
    touch_profile = safe_height() > safe_width()
        ? TOUCH_LAYOUT_PORTRAIT : TOUCH_LAYOUT_LANDSCAPE;

    if (old_width != screen_width || old_height != screen_height
        || old_left != safe_left || old_top != safe_top
        || old_right != safe_right || old_bottom != safe_bottom
        || old_pixels_per_point != screen_pixels_per_point
        || old_profile != touch_profile) {
        // Coordinates from the old orientation no longer describe the same
        // physical points. Cancel them rather than leaving a control held.
        touch_forget_fingers();
    }
}

void touch_get_safe_area(int *left, int *top, int *right, int *bottom) {
    if (left != NULL) {
        *left = safe_left;
    }
    if (top != NULL) {
        *top = safe_top;
    }
    if (right != NULL) {
        *right = safe_right;
    }
    if (bottom != NULL) {
        *bottom = safe_bottom;
    }
}

void touch_set_haptic_callback(void (*callback)(void)) {
    haptic_callback = callback;
}

void touch_set_screen_size(int width, int height) {
    touch_set_screen_geometry(width, height, 0, 0, 0, 0, 1.0f);
}

static struct Finger *find_finger(long long id) {
    for (int i = 0; i < MAX_FINGERS; i++) {
        if (fingers[i].active && fingers[i].id == id) {
            return &fingers[i];
        }
    }
    return NULL;
}

static struct Finger *find_stick_finger(void) {
    for (int i = 0; i < MAX_FINGERS; i++) {
        if (fingers[i].active && fingers[i].role == ROLE_STICK) {
            return &fingers[i];
        }
    }
    return NULL;
}

static struct Finger *alloc_finger(long long id) {
    for (int i = 0; i < MAX_FINGERS; i++) {
        if (!fingers[i].active) {
            fingers[i].active = true;
            fingers[i].id = id;
            return &fingers[i];
        }
    }
    return NULL;
}

//==============================================================================
// Layout: persistence and editing
//==============================================================================

#define TOUCH_LAYOUT_FILE "sm64_touch_layout.txt"


static bool layout_edit_mode;
static bool layout_dirty;

// The button currently being dragged, or -1
static int layout_dragged_button(void) {
    for (int i = 0; i < MAX_FINGERS; i++) {
        if (fingers[i].active && fingers[i].role == ROLE_LAYOUT_DRAG) {
            return fingers[i].button;
        }
    }
    return -1;
}

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// Places a dragged button at a whole-window normalized coordinate, keeping
// the entire drawn circle within the current UIKit safe area.
static float button_radius(const struct TouchButton *b);
static void button_center(const struct TouchButton *b, float *x, float *y);

static void layout_place_button(struct TouchButton *b, float x, float y) {
    float r = button_radius(b);
    float min_x = (float) safe_left + r;
    float max_x = (float) (safe_left + safe_width()) - r;
    float min_y = (float) safe_top + r;
    float max_y = (float) (safe_top + safe_height()) - r;
    float px = x * (float) screen_width;
    float py = y * (float) screen_height;

    if (min_x > max_x) {
        min_x = max_x = (float) safe_left + (float) safe_width() * 0.5f;
    }
    if (min_y > max_y) {
        min_y = max_y = (float) safe_top + (float) safe_height() * 0.5f;
    }
    px = clampf(px, min_x, max_x);
    py = clampf(py, min_y, max_y);
    b->cx = (px - (float) safe_left) / (float) safe_width();
    b->cy = (py - (float) safe_top) / (float) safe_height();
}

void touch_layout_reset(void) {
    memcpy(current_buttons(), touch_button_defaults[touch_profile],
           sizeof(touch_buttons[touch_profile]));
    layout_dirty = true;
}

// Reads back a saved layout. Anything unrecognized or out of range is
// ignored rather than rejected wholesale, so a partly stale file (say from
// a build with different buttons) still restores what it can.
static void touch_layout_load(void) {
    char line[128];
    FILE *f;

    memcpy(touch_buttons, touch_button_defaults, sizeof(touch_buttons));
    layout_dirty = false;

    f = fopen(fs_get_write_path(TOUCH_LAYOUT_FILE), "r");
    if (f == NULL) {
        return;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        char profile_name[32];
        char name[32];
        float cx, cy, r;
        int profile = TOUCH_LAYOUT_LANDSCAPE;
        int i;

        if (sscanf(line, "%31s %31s %f %f %f", profile_name, name, &cx, &cy, &r) == 5) {
            for (profile = 0; profile < TOUCH_LAYOUT_PROFILE_COUNT; profile++) {
                if (strcmp(profile_name, touch_profile_names[profile]) == 0) {
                    break;
                }
            }
            if (profile == TOUCH_LAYOUT_PROFILE_COUNT) {
                continue;
            }
        } else {
            // Version 1 files had no profile. Their coordinates describe the
            // old landscape-only layout, so preserve them as landscape.
            profile = TOUCH_LAYOUT_LANDSCAPE;
            if (sscanf(line, "%31s %f %f %f", name, &cx, &cy, &r) != 4) {
                continue;
            }
        }
        for (i = 0; i < TOUCH_BUTTON_COUNT; i++) {
            if (strcmp(name, touch_button_names[i]) == 0) {
                if (cx >= 0.0f && cx <= 1.0f && cy >= 0.0f && cy <= 1.0f
                    && r > 0.005f && r < 0.5f) {
                    touch_buttons[profile][i].cx = cx;
                    touch_buttons[profile][i].cy = cy;
                    touch_buttons[profile][i].r = r;
                }
                break;
            }
        }
    }
    fclose(f);
}

void touch_layout_save(void) {
    const char *path = fs_get_write_path(TOUCH_LAYOUT_FILE);
    FILE *f;
    int profile, i;

    if (!layout_dirty) {
        return;
    }
    f = fs_open_atomic(path);
    if (f == NULL) {
        return;
    }
    fprintf(f, "# On-screen control layout: profile name center-x center-y radius\n");
    fprintf(f, "# Coordinates are normalized within the current safe area.\n");
    fprintf(f, "# Delete this file to go back to the default layout.\n");
    for (profile = 0; profile < TOUCH_LAYOUT_PROFILE_COUNT; profile++) {
        for (i = 0; i < TOUCH_BUTTON_COUNT; i++) {
            fprintf(f, "%s %s %.4f %.4f %.4f\n",
                    touch_profile_names[profile], touch_button_names[i],
                    touch_buttons[profile][i].cx, touch_buttons[profile][i].cy,
                    touch_buttons[profile][i].r);
        }
    }
    if (fs_close_atomic(f, path)) {
        layout_dirty = false;
    }
}

void touch_layout_edit_set(bool on) {
    if (layout_edit_mode == on) {
        return;
    }
    layout_edit_mode = on;
    if (!on) {
        touch_layout_save();
    }
}

bool touch_layout_edit_active(void) {
    return layout_edit_mode;
}

// Distance test in pixel space so circles stay circular regardless of the
// screen's aspect ratio
// The drawn radius, with the player's size setting applied. The stored
// value stays as authored so changing the setting never compounds.
static float button_radius(const struct TouchButton *b) {
    float scale = configTouchScale;

    if (!(scale > 0.1f)) {
        scale = 1.0f; // also catches a NaN from a hand-edited config
    }
    if (scale > 3.0f) {
        scale = 3.0f;
    }
    return b->r * scale * control_unit();
}

static void button_center(const struct TouchButton *b, float *x, float *y) {
    float r = button_radius(b);
    float min_x = (float) safe_left + r;
    float max_x = (float) (safe_left + safe_width()) - r;
    float min_y = (float) safe_top + r;
    float max_y = (float) (safe_top + safe_height()) - r;

    if (min_x > max_x) {
        min_x = max_x = (float) safe_left + (float) safe_width() * 0.5f;
    }
    if (min_y > max_y) {
        min_y = max_y = (float) safe_top + (float) safe_height() * 0.5f;
    }
    *x = clampf((float) safe_left + b->cx * (float) safe_width(), min_x, max_x);
    *y = clampf((float) safe_top + b->cy * (float) safe_height(), min_y, max_y);
}

static bool hit_button(const struct TouchButton *b, float x, float y) {
    float cx, cy;
    button_center(b, &cx, &cy);
    float dx = x * (float) screen_width - cx;
    float dy = y * (float) screen_height - cy;
    // Generous hit area: 1.4x the drawn radius
    float r = button_radius(b) * 1.4f;
    return dx * dx + dy * dy <= r * r;
}

void touch_down(long long finger_id, float x, float y) {
    struct Finger *f = find_finger(finger_id);
    if (f == NULL) {
        f = alloc_finger(finger_id);
    }
    if (f == NULL) {
        return;
    }
    f->x = x;
    f->y = y;
    f->role = ROLE_NONE;
    f->moved = false;

    if (layout_edit_mode) {
        // Grab a button to reposition it. A touch that lands on nothing is
        // how the player signals they are done, once it lifts without
        // having dragged anything.
        for (int i = 0; i < TOUCH_BUTTON_COUNT; i++) {
            if (hit_button(&current_buttons()[i], x, y)) {
                f->role = ROLE_LAYOUT_DRAG;
                f->button = i;
                return;
            }
        }
        f->role = ROLE_LAYOUT_TAP;
        return;
    }

    for (int i = 0; i < TOUCH_BUTTON_COUNT; i++) {
        if (hit_button(&current_buttons()[i], x, y)) {
            f->role = ROLE_BUTTON;
            f->button = i;
            if (configTouchHaptics && haptic_callback != NULL) {
                haptic_callback();
            }
            return;
        }
    }

    {
        float px = x * (float) screen_width;
        float py = y * (float) screen_height;
        float sx = (px - (float) safe_left) / (float) safe_width();
        float sy = (py - (float) safe_top) / (float) safe_height();
        if (find_stick_finger() == NULL
            && sx >= 0.0f && sx <= 1.0f && sy >= 0.0f && sy <= 1.0f
            && sx < STICK_ZONE_X && sy > STICK_ZONE_Y) {
            float range = STICK_RANGE * control_unit();
            float min_x = (float) safe_left + range;
            float max_x = (float) (safe_left + safe_width()) - range;
            float min_y = (float) safe_top + range;
            float max_y = (float) (safe_top + safe_height()) - range;
            if (min_x <= max_x) {
                px = clampf(px, min_x, max_x);
            }
            if (min_y <= max_y) {
                py = clampf(py, min_y, max_y);
            }
            f->role = ROLE_STICK;
            f->origin_x = px / (float) screen_width;
            f->origin_y = py / (float) screen_height;
        }
    }
}

void touch_motion(long long finger_id, float x, float y) {
    struct Finger *f = find_finger(finger_id);
    if (f == NULL) {
        return;
    }
    f->x = x;
    f->y = y;

    if (f->role == ROLE_LAYOUT_DRAG) {
        layout_place_button(&current_buttons()[f->button], x, y);
        f->moved = true;
        layout_dirty = true;
    }
}

void touch_up(long long finger_id) {
    struct Finger *f = find_finger(finger_id);

    if (f != NULL) {
        // Only a finger that both landed and lifted inside edit mode ends
        // it; the one that pressed A to start editing is still down here
        // and must not count. Nor should a resting palm end the mode out
        // from under a drag another finger is in the middle of.
        if (f->role == ROLE_LAYOUT_TAP && !f->moved && layout_dragged_button() < 0) {
            touch_layout_edit_set(false);
        }
        f->active = false;
        f->role = ROLE_NONE;
        f->moved = false;
    }
}

void touch_forget_fingers(void) {
    memset(fingers, 0, sizeof(fingers));
}

static void touch_init(void) {
    memset(fingers, 0, sizeof(fingers));
    touch_layout_load();
}

static void touch_read(OSContPad *pad) {
    for (int i = 0; i < MAX_FINGERS; i++) {
        struct Finger *f = &fingers[i];
        if (!f->active) {
            continue;
        }
        if (f->role == ROLE_BUTTON) {
            pad->button |= current_buttons()[f->button].mask;
        } else if (f->role == ROLE_STICK) {
            float range = STICK_RANGE * control_unit();
            float dx = (f->x - f->origin_x) * (float) screen_width / range;
            float dy = (f->y - f->origin_y) * (float) screen_height / range;
            float mag = sqrtf(dx * dx + dy * dy);
            if (mag > 1.0f) {
                dx /= mag;
                dy /= mag;
            }
            // Game expects stick coordinates within -80..80
            pad->stick_x = (s8)(dx * 80.0f);
            pad->stick_y = (s8)(-dy * 80.0f);
        }
    }
}

struct ControllerAPI controller_touch = {
    touch_init,
    touch_read
};

//==============================================================================
// Overlay geometry (renderer-independent)
//==============================================================================

#define CIRCLE_SEGMENTS 24
// pos (2 floats) + color (4 floats)
#define FLOATS_PER_VERTEX 6
// stick base + stick nub + buttons, all circles, plus 4 direction arrows
#define MAX_VERTS ((2 + TOUCH_BUTTON_COUNT) * CIRCLE_SEGMENTS * 3 + 4 * 3)

static float overlay_verts[MAX_VERTS * FLOATS_PER_VERTEX];
static int overlay_num_verts;

#ifdef ENABLE_OPENGL

static GLuint overlay_program;
static GLuint overlay_vbo;
static bool overlay_inited;
static bool overlay_failed;

static const char overlay_vs[] =
    "#version 100\n"
    "attribute vec2 aPos;\n"
    "attribute vec4 aColor;\n"
    "varying vec4 vColor;\n"
    "void main() {\n"
    "    vColor = aColor;\n"
    "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
    "}\n";

static const char overlay_fs[] =
    "#version 100\n"
    "precision mediump float;\n"
    "varying vec4 vColor;\n"
    "void main() {\n"
    "    gl_FragColor = vColor;\n"
    "}\n";

static GLuint overlay_compile_shader(GLenum type, const char *src) {
    GLuint shader = glCreateShader(type);
    GLint compiled = GL_FALSE;
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE) {
        char log[1024];
        GLsizei length = 0;
        glGetShaderInfoLog(shader, sizeof(log), &length, log);
        fprintf(stderr, "Touch overlay shader compilation failed: %.*s\n",
                (int) length, log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static void overlay_init(void) {
    GLuint vs = overlay_compile_shader(GL_VERTEX_SHADER, overlay_vs);
    GLuint fs = overlay_compile_shader(GL_FRAGMENT_SHADER, overlay_fs);
    GLint linked = GL_FALSE;
    if (vs == 0 || fs == 0) {
        glDeleteShader(vs);
        glDeleteShader(fs);
        overlay_failed = true;
        return;
    }
    overlay_program = glCreateProgram();
    glAttachShader(overlay_program, vs);
    glAttachShader(overlay_program, fs);
    glBindAttribLocation(overlay_program, 0, "aPos");
    glBindAttribLocation(overlay_program, 1, "aColor");
    glLinkProgram(overlay_program);
    glGetProgramiv(overlay_program, GL_LINK_STATUS, &linked);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (linked != GL_TRUE) {
        char log[1024];
        GLsizei length = 0;
        glGetProgramInfoLog(overlay_program, sizeof(log), &length, log);
        fprintf(stderr, "Touch overlay program link failed: %.*s\n", (int) length, log);
        glDeleteProgram(overlay_program);
        overlay_program = 0;
        overlay_failed = true;
        return;
    }

    glGenBuffers(1, &overlay_vbo);
    overlay_inited = true;
}

#endif // ENABLE_OPENGL

static void overlay_push_vertex(float px, float py, const float color[4], float alpha) {
    if (overlay_num_verts >= MAX_VERTS) {
        return;
    }
    float *v = &overlay_verts[overlay_num_verts * FLOATS_PER_VERTEX];
    v[0] = 2.0f * px / (float) screen_width - 1.0f;
    v[1] = 1.0f - 2.0f * py / (float) screen_height;
    v[2] = color[0];
    v[3] = color[1];
    v[4] = color[2];
    v[5] = alpha;
    overlay_num_verts++;
}

static void overlay_push_circle(float cx, float cy, float r, const float color[4], float alpha) {
    for (int i = 0; i < CIRCLE_SEGMENTS; i++) {
        float a0 = 2.0f * (float) M_PI * (float) i / CIRCLE_SEGMENTS;
        float a1 = 2.0f * (float) M_PI * (float) (i + 1) / CIRCLE_SEGMENTS;
        overlay_push_vertex(cx, cy, color, alpha);
        overlay_push_vertex(cx + cosf(a0) * r, cy + sinf(a0) * r, color, alpha);
        overlay_push_vertex(cx + cosf(a1) * r, cy + sinf(a1) * r, color, alpha);
    }
}

// Small triangle pointing away from the C cluster's center, drawn on top of
// each C button so the four buttons read as directions
static void overlay_push_arrow(float cx, float cy, float r, float dir_x, float dir_y, float alpha) {
    static const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    float tip_x = cx + dir_x * r * 0.6f;
    float tip_y = cy + dir_y * r * 0.6f;
    float base_x = cx - dir_x * r * 0.2f;
    float base_y = cy - dir_y * r * 0.2f;
    // perpendicular
    float px = -dir_y * r * 0.4f;
    float py = dir_x * r * 0.4f;
    overlay_push_vertex(tip_x, tip_y, white, alpha);
    overlay_push_vertex(base_x + px, base_y + py, white, alpha);
    overlay_push_vertex(base_x - px, base_y - py, white, alpha);
}

static bool button_is_held(int button) {
    for (int i = 0; i < MAX_FINGERS; i++) {
        if (fingers[i].active && fingers[i].role == ROLE_BUTTON && fingers[i].button == button) {
            return true;
        }
    }
    return false;
}

static void overlay_build(void) {
    float w = (float) screen_width;
    float h = (float) screen_height;
    float unit = control_unit();
    static const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    float opacity = configTouchOpacity;
    bool editing = layout_edit_mode;
    int dragged = layout_dragged_button();

    overlay_num_verts = 0;

    if (configTouchAutoHide && controller_gamepad_is_connected() && !editing) {
        // Keep touch hit-testing live so the player can still reach a button,
        // but remove visual clutter while a hardware controller is attached.
        return;
    }

    if (!(opacity > 0.0f)) {
        // Hidden (or a NaN from a hand-edited config). Input still works;
        // this is for playing with a hardware controller attached.
        if (!editing) {
            return;
        }
        opacity = 1.0f;
    }
    if (opacity > 2.0f) {
        opacity = 2.0f;
    }
    if (editing) {
        // Show the whole layout clearly regardless of the opacity setting
        opacity = 2.0f;
    }

    // Stick: while held, draw base ring at the origin and nub at the finger;
    // otherwise a faint resting hint
    bool stick_held = false;
    for (int i = 0; i < MAX_FINGERS && !editing; i++) {
        struct Finger *f = &fingers[i];
        if (f->active && f->role == ROLE_STICK) {
            float range = STICK_RANGE * unit;
            float ox = f->origin_x * w;
            float oy = f->origin_y * h;
            float dx = f->x * w - ox;
            float dy = f->y * h - oy;
            float mag = sqrtf(dx * dx + dy * dy);
            if (mag > range) {
                dx *= range / mag;
                dy *= range / mag;
            }
            overlay_push_circle(ox, oy, range, white, 0.15f * opacity);
            overlay_push_circle(ox + dx, oy + dy, range * 0.45f, white, 0.35f * opacity);
            stick_held = true;
            break;
        }
    }
    if (!stick_held) {
        float stick_x = touch_profile == TOUCH_LAYOUT_PORTRAIT ? 0.220f : 0.180f;
        float stick_y = touch_profile == TOUCH_LAYOUT_PORTRAIT ? 0.800f : 0.680f;
        float cx = (float) safe_left + stick_x * (float) safe_width();
        float cy = (float) safe_top + stick_y * (float) safe_height();
        overlay_push_circle(cx, cy, STICK_RANGE * unit, white, 0.08f * opacity);
        overlay_push_circle(cx, cy, STICK_RANGE * unit * 0.45f, white, 0.15f * opacity);
    }

    for (int i = 0; i < TOUCH_BUTTON_COUNT; i++) {
        const struct TouchButton *b = &current_buttons()[i];
        float cx, cy;
        float alpha;

        if (editing) {
            alpha = (i == dragged) ? 0.85f : 0.45f;
        } else {
            alpha = (button_is_held(i) ? 0.55f : 0.28f) * opacity;
        }
        button_center(b, &cx, &cy);
        overlay_push_circle(cx, cy, button_radius(b), b->color, alpha);
    }

    {
        float arrow_alpha_base = editing ? 0.6f : 0.5f * opacity;
        static const struct { int id; float dx, dy; } arrows[] = {
            { TOUCH_C_UP, 0.0f, -1.0f },   { TOUCH_C_DOWN, 0.0f, 1.0f },
            { TOUCH_C_LEFT, -1.0f, 0.0f }, { TOUCH_C_RIGHT, 1.0f, 0.0f },
        };
        for (size_t i = 0; i < sizeof(arrows) / sizeof(arrows[0]); i++) {
            const struct TouchButton *b = &current_buttons()[arrows[i].id];
            float cx, cy;
            button_center(b, &cx, &cy);
            overlay_push_arrow(cx, cy, button_radius(b), arrows[i].dx,
                               arrows[i].dy, arrow_alpha_base);
        }
    }
}

// Builds the overlay for the current touch state and returns the vertex
// data: num_verts vertices, interleaved as [x, y, r, g, b, a] with x/y in
// normalized device coordinates. Used directly by the Metal backend; the
// OpenGL path below renders the same data itself.
const float *touch_overlay_build(int width, int height, int *num_verts) {
    if (width > 0 && height > 0 && (width != screen_width || height != screen_height)) {
        // Rendering can observe a new drawable one frame before SDL delivers
        // its resize event. Preserve the safe-area proportions until UIKit's
        // exact insets are queried by the event pump.
        float left_fraction = (float) safe_left / (float) screen_width;
        float top_fraction = (float) safe_top / (float) screen_height;
        float right_fraction = (float) safe_right / (float) screen_width;
        float bottom_fraction = (float) safe_bottom / (float) screen_height;
        touch_set_screen_geometry(width, height,
                                  (int) lroundf(left_fraction * (float) width),
                                  (int) lroundf(top_fraction * (float) height),
                                  (int) lroundf(right_fraction * (float) width),
                                  (int) lroundf(bottom_fraction * (float) height),
                                  screen_pixels_per_point);
    }
    overlay_build();
    *num_verts = overlay_num_verts;
    return overlay_verts;
}

#ifdef ENABLE_OPENGL

// The game's renderer (gfx_opengl.c) caches GL state across frames: the
// current program, its VBO binding, vertex attrib pointers, viewport and
// enable flags are only re-specified when the renderer thinks they changed.
// Everything the overlay touches therefore has to be saved and restored
// exactly.
struct SavedAttrib {
    GLint enabled;
    GLint size;
    GLint stride;
    GLint type;
    GLint normalized;
    GLint buffer;
    void *pointer;
};

static void save_attrib(GLuint index, struct SavedAttrib *a) {
    glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &a->enabled);
    glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_SIZE, &a->size);
    glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &a->stride);
    glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_TYPE, &a->type);
    glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &a->normalized);
    glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &a->buffer);
    glGetVertexAttribPointerv(index, GL_VERTEX_ATTRIB_ARRAY_POINTER, &a->pointer);
}

static void restore_attrib(GLuint index, const struct SavedAttrib *a) {
    glBindBuffer(GL_ARRAY_BUFFER, a->buffer);
    if (a->size != 0) {
        glVertexAttribPointer(index, a->size, a->type, a->normalized, a->stride, a->pointer);
    }
    if (a->enabled) {
        glEnableVertexAttribArray(index);
    } else {
        glDisableVertexAttribArray(index);
    }
}

void touch_render_overlay(int width, int height) {
    int num_verts;
    touch_overlay_build(width, height, &num_verts);
    if (num_verts == 0) {
        return;
    }

    if (!overlay_inited && !overlay_failed) {
        overlay_init();
    }
    if (!overlay_inited) {
        return;
    }

    GLint prev_program, prev_array_buffer, prev_viewport[4];
    GLint prev_blend_src_rgb, prev_blend_dst_rgb, prev_blend_src_alpha, prev_blend_dst_alpha;
    GLint prev_blend_equation_rgb, prev_blend_equation_alpha;
    struct SavedAttrib prev_attrib[2];
    GLboolean prev_depth_test = glIsEnabled(GL_DEPTH_TEST);
    GLboolean prev_scissor = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean prev_blend = glIsEnabled(GL_BLEND);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prev_program);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prev_array_buffer);
    glGetIntegerv(GL_VIEWPORT, prev_viewport);
    glGetIntegerv(GL_BLEND_SRC_RGB, &prev_blend_src_rgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &prev_blend_dst_rgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &prev_blend_src_alpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &prev_blend_dst_alpha);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &prev_blend_equation_rgb);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &prev_blend_equation_alpha);
    save_attrib(0, &prev_attrib[0]);
    save_attrib(1, &prev_attrib[1]);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glViewport(0, 0, screen_width, screen_height);

    glUseProgram(overlay_program);
    glBindBuffer(GL_ARRAY_BUFFER, overlay_vbo);
    glBufferData(GL_ARRAY_BUFFER, overlay_num_verts * FLOATS_PER_VERTEX * sizeof(float),
                 overlay_verts, GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, FLOATS_PER_VERTEX * sizeof(float), (void *) 0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, FLOATS_PER_VERTEX * sizeof(float),
                          (void *) (2 * sizeof(float)));
    glDrawArrays(GL_TRIANGLES, 0, overlay_num_verts);

    restore_attrib(0, &prev_attrib[0]);
    restore_attrib(1, &prev_attrib[1]);
    glBindBuffer(GL_ARRAY_BUFFER, prev_array_buffer);
    glUseProgram(prev_program);
    glBlendEquationSeparate(prev_blend_equation_rgb, prev_blend_equation_alpha);
    glBlendFuncSeparate(prev_blend_src_rgb, prev_blend_dst_rgb,
                        prev_blend_src_alpha, prev_blend_dst_alpha);
    glViewport(prev_viewport[0], prev_viewport[1], prev_viewport[2], prev_viewport[3]);
    if (prev_depth_test) {
        glEnable(GL_DEPTH_TEST);
    }
    if (prev_scissor) {
        glEnable(GL_SCISSOR_TEST);
    }
    if (!prev_blend) {
        glDisable(GL_BLEND);
    }
}

#endif // ENABLE_OPENGL

#endif // TARGET_IOS
