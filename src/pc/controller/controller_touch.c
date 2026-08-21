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
#include <string.h>

#include <SDL2/SDL.h>

#ifdef ENABLE_OPENGL
#define GL_GLEXT_PROTOTYPES 1
#include <SDL2/SDL_opengles2.h>
#endif

#include <ultra64.h>

#include "controller_api.h"
#include "controller_touch.h"

#define MAX_FINGERS 10

// Full stick deflection distance, as a fraction of screen height
#define STICK_RANGE 0.12f
// Fingers landing left of this (and below the start button) control the stick
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
    float cx; // center, fraction of screen width
    float cy; // center, fraction of screen height
    float r;  // radius, fraction of screen height
    uint16_t mask;
    float color[4];
};

// Colors follow the N64 pad: A blue, B green, C yellow, Start red
static const struct TouchButton touch_buttons[TOUCH_BUTTON_COUNT] = {
    [TOUCH_A]       = { 0.905f, 0.720f, 0.085f, A_BUTTON,     { 0.25f, 0.35f, 0.95f, 1.0f } },
    [TOUCH_B]       = { 0.780f, 0.860f, 0.065f, B_BUTTON,     { 0.20f, 0.80f, 0.30f, 1.0f } },
    [TOUCH_Z]       = { 0.660f, 0.700f, 0.055f, Z_TRIG,       { 0.60f, 0.60f, 0.65f, 1.0f } },
    [TOUCH_R]       = { 0.945f, 0.095f, 0.050f, R_TRIG,       { 0.60f, 0.60f, 0.65f, 1.0f } },
    [TOUCH_START]   = { 0.500f, 0.085f, 0.050f, START_BUTTON, { 0.90f, 0.25f, 0.25f, 1.0f } },
    [TOUCH_C_UP]    = { 0.860f, 0.330f, 0.042f, U_CBUTTONS,   { 0.95f, 0.80f, 0.15f, 1.0f } },
    [TOUCH_C_DOWN]  = { 0.860f, 0.510f, 0.042f, D_CBUTTONS,   { 0.95f, 0.80f, 0.15f, 1.0f } },
    [TOUCH_C_LEFT]  = { 0.772f, 0.420f, 0.042f, L_CBUTTONS,   { 0.95f, 0.80f, 0.15f, 1.0f } },
    [TOUCH_C_RIGHT] = { 0.948f, 0.420f, 0.042f, R_CBUTTONS,   { 0.95f, 0.80f, 0.15f, 1.0f } },
};

enum FingerRole {
    ROLE_NONE,
    ROLE_STICK,
    ROLE_BUTTON
};

struct Finger {
    bool active;
    long long id;
    enum FingerRole role;
    int button; // valid when role == ROLE_BUTTON
    float x, y; // normalized position
    float origin_x, origin_y; // stick neutral position (normalized)
};

static struct Finger fingers[MAX_FINGERS];

static int screen_width = 1;
static int screen_height = 1;

void touch_set_screen_size(int width, int height) {
    screen_width = width > 0 ? width : 1;
    screen_height = height > 0 ? height : 1;
}

static struct Finger *find_finger(long long id) {
    for (int i = 0; i < MAX_FINGERS; i++) {
        if (fingers[i].active && fingers[i].id == id) {
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

// Distance test in pixel space so circles stay circular regardless of the
// screen's aspect ratio
static bool hit_button(const struct TouchButton *b, float x, float y) {
    float dx = (x - b->cx) * (float) screen_width;
    float dy = (y - b->cy) * (float) screen_height;
    float r = b->r * (float) screen_height;
    // Generous hit area: 1.4x the drawn radius
    r *= 1.4f;
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

    for (int i = 0; i < TOUCH_BUTTON_COUNT; i++) {
        if (hit_button(&touch_buttons[i], x, y)) {
            f->role = ROLE_BUTTON;
            f->button = i;
            return;
        }
    }

    if (x < STICK_ZONE_X && y > STICK_ZONE_Y) {
        f->role = ROLE_STICK;
        f->origin_x = x;
        f->origin_y = y;
    }
}

void touch_motion(long long finger_id, float x, float y) {
    struct Finger *f = find_finger(finger_id);
    if (f == NULL) {
        return;
    }
    f->x = x;
    f->y = y;
}

void touch_up(long long finger_id) {
    struct Finger *f = find_finger(finger_id);
    if (f != NULL) {
        f->active = false;
        f->role = ROLE_NONE;
    }
}

static void touch_init(void) {
    memset(fingers, 0, sizeof(fingers));
}

static void touch_read(OSContPad *pad) {
    for (int i = 0; i < MAX_FINGERS; i++) {
        struct Finger *f = &fingers[i];
        if (!f->active) {
            continue;
        }
        if (f->role == ROLE_BUTTON) {
            pad->button |= touch_buttons[f->button].mask;
        } else if (f->role == ROLE_STICK) {
            float range = STICK_RANGE * (float) screen_height;
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
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);
    return shader;
}

static void overlay_init(void) {
    GLuint vs = overlay_compile_shader(GL_VERTEX_SHADER, overlay_vs);
    GLuint fs = overlay_compile_shader(GL_FRAGMENT_SHADER, overlay_fs);
    overlay_program = glCreateProgram();
    glAttachShader(overlay_program, vs);
    glAttachShader(overlay_program, fs);
    glBindAttribLocation(overlay_program, 0, "aPos");
    glBindAttribLocation(overlay_program, 1, "aColor");
    glLinkProgram(overlay_program);
    glDeleteShader(vs);
    glDeleteShader(fs);

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
    static const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

    overlay_num_verts = 0;

    // Stick: while held, draw base ring at the origin and nub at the finger;
    // otherwise a faint resting hint
    bool stick_held = false;
    for (int i = 0; i < MAX_FINGERS; i++) {
        struct Finger *f = &fingers[i];
        if (f->active && f->role == ROLE_STICK) {
            float range = STICK_RANGE * h;
            float ox = f->origin_x * w;
            float oy = f->origin_y * h;
            float dx = f->x * w - ox;
            float dy = f->y * h - oy;
            float mag = sqrtf(dx * dx + dy * dy);
            if (mag > range) {
                dx *= range / mag;
                dy *= range / mag;
            }
            overlay_push_circle(ox, oy, range, white, 0.15f);
            overlay_push_circle(ox + dx, oy + dy, range * 0.45f, white, 0.35f);
            stick_held = true;
            break;
        }
    }
    if (!stick_held) {
        overlay_push_circle(0.18f * w, 0.68f * h, STICK_RANGE * h, white, 0.08f);
        overlay_push_circle(0.18f * w, 0.68f * h, STICK_RANGE * h * 0.45f, white, 0.15f);
    }

    for (int i = 0; i < TOUCH_BUTTON_COUNT; i++) {
        const struct TouchButton *b = &touch_buttons[i];
        float alpha = button_is_held(i) ? 0.55f : 0.28f;
        overlay_push_circle(b->cx * w, b->cy * h, b->r * h, b->color, alpha);
    }

    float arrow_alpha_base = 0.5f;
    overlay_push_arrow(touch_buttons[TOUCH_C_UP].cx * w, touch_buttons[TOUCH_C_UP].cy * h,
                       touch_buttons[TOUCH_C_UP].r * h, 0.0f, -1.0f, arrow_alpha_base);
    overlay_push_arrow(touch_buttons[TOUCH_C_DOWN].cx * w, touch_buttons[TOUCH_C_DOWN].cy * h,
                       touch_buttons[TOUCH_C_DOWN].r * h, 0.0f, 1.0f, arrow_alpha_base);
    overlay_push_arrow(touch_buttons[TOUCH_C_LEFT].cx * w, touch_buttons[TOUCH_C_LEFT].cy * h,
                       touch_buttons[TOUCH_C_LEFT].r * h, -1.0f, 0.0f, arrow_alpha_base);
    overlay_push_arrow(touch_buttons[TOUCH_C_RIGHT].cx * w, touch_buttons[TOUCH_C_RIGHT].cy * h,
                       touch_buttons[TOUCH_C_RIGHT].r * h, 1.0f, 0.0f, arrow_alpha_base);
}

// Builds the overlay for the current touch state and returns the vertex
// data: num_verts vertices, interleaved as [x, y, r, g, b, a] with x/y in
// normalized device coordinates. Used directly by the Metal backend; the
// OpenGL path below renders the same data itself.
const float *touch_overlay_build(int width, int height, int *num_verts) {
    touch_set_screen_size(width, height);
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

    if (!overlay_inited) {
        overlay_init();
    }

    GLint prev_program, prev_array_buffer, prev_viewport[4];
    struct SavedAttrib prev_attrib[2];
    GLboolean prev_depth_test = glIsEnabled(GL_DEPTH_TEST);
    GLboolean prev_scissor = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean prev_blend = glIsEnabled(GL_BLEND);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prev_program);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prev_array_buffer);
    glGetIntegerv(GL_VIEWPORT, prev_viewport);
    save_attrib(0, &prev_attrib[0]);
    save_attrib(1, &prev_attrib[1]);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
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
