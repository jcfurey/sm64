#include "../compat.h"

#if !defined(__linux__) && !defined(__BSD__) && (defined(ENABLE_OPENGL) || defined(ENABLE_METAL))

#ifdef __MINGW32__
#define FOR_WINDOWS 1
#else
#define FOR_WINDOWS 0
#endif

#ifdef ENABLE_OPENGL
#if FOR_WINDOWS
#include <GL/glew.h>
#include "SDL.h"
#define GL_GLEXT_PROTOTYPES 1
#include "SDL_opengl.h"
#else
#include <SDL2/SDL.h>
#define GL_GLEXT_PROTOTYPES 1
#include <SDL2/SDL_opengles2.h>
#endif
#else
#include <SDL2/SDL.h>
#endif

#include <stdio.h>
#include <stdlib.h>

#include "gfx_window_manager_api.h"
#include "gfx_screen_config.h"
#include "../configfile.h"

#ifdef TARGET_IOS
#include "../ios_support.h"
#endif

#ifdef ENABLE_METAL
#include "gfx_metal.h"
#endif

#ifdef TARGET_IOS
#include "../controller/controller_touch.h"

// Project extension carried by ios/patches/SDL2-2.30.7-uiscene.patch.
// Values are fractions of the UIKit view, which we map onto the current
// native-resolution drawable below.
extern int SDL_iPhoneGetWindowSafeAreaInsets(SDL_Window *window,
                                             float *left, float *top,
                                             float *right, float *bottom,
                                             float *view_width, float *view_height);
#endif

#ifdef HIGH_FPS_PC
#include "../framerate.h"
#endif

#ifdef ENABLE_METAL
#define GFX_API_NAME "SDL2 - Metal"
#else
#define GFX_API_NAME "SDL2 - OpenGL"
#endif

static SDL_Window *wnd;
#ifdef ENABLE_OPENGL
static SDL_GLContext gl_context;
#endif
static int inverted_scancode_table[512];
static int vsync_enabled = 0;
static unsigned int window_width = DESIRED_SCREEN_WIDTH;
static unsigned int window_height = DESIRED_SCREEN_HEIGHT;
static bool fullscreen_state;
static void (*on_fullscreen_changed_callback)(bool is_now_fullscreen);
static bool (*on_key_down_callback)(int scancode);
static bool (*on_key_up_callback)(int scancode);
static void (*on_all_keys_up_callback)(void);
#ifdef TARGET_IOS
static void (*ios_run_one_game_iter)(void);
static void SDLCALL gfx_sdl_ios_animation_callback(void *param);
#endif

static void gfx_sdl_fatal(const char *action) {
    fprintf(stderr, "Fatal SDL video error while %s: %s\n", action, SDL_GetError());
    SDL_Quit();
    exit(EXIT_FAILURE);
}

const SDL_Scancode windows_scancode_table[] =
{ 
    /*	0						1							2							3							4						5							6							7 */
    /*	8						9							A							B							C						D							E							F */
    SDL_SCANCODE_UNKNOWN,		SDL_SCANCODE_ESCAPE,		SDL_SCANCODE_1,				SDL_SCANCODE_2,				SDL_SCANCODE_3,			SDL_SCANCODE_4,				SDL_SCANCODE_5,				SDL_SCANCODE_6,			/* 0 */
    SDL_SCANCODE_7,				SDL_SCANCODE_8,				SDL_SCANCODE_9,				SDL_SCANCODE_0,				SDL_SCANCODE_MINUS,		SDL_SCANCODE_EQUALS,		SDL_SCANCODE_BACKSPACE,		SDL_SCANCODE_TAB,		/* 0 */

    SDL_SCANCODE_Q,				SDL_SCANCODE_W,				SDL_SCANCODE_E,				SDL_SCANCODE_R,				SDL_SCANCODE_T,			SDL_SCANCODE_Y,				SDL_SCANCODE_U,				SDL_SCANCODE_I,			/* 1 */
    SDL_SCANCODE_O,				SDL_SCANCODE_P,				SDL_SCANCODE_LEFTBRACKET,	SDL_SCANCODE_RIGHTBRACKET,	SDL_SCANCODE_RETURN,	SDL_SCANCODE_LCTRL,			SDL_SCANCODE_A,				SDL_SCANCODE_S,			/* 1 */

    SDL_SCANCODE_D,				SDL_SCANCODE_F,				SDL_SCANCODE_G,				SDL_SCANCODE_H,				SDL_SCANCODE_J,			SDL_SCANCODE_K,				SDL_SCANCODE_L,				SDL_SCANCODE_SEMICOLON,	/* 2 */
    SDL_SCANCODE_APOSTROPHE,	SDL_SCANCODE_GRAVE,			SDL_SCANCODE_LSHIFT,		SDL_SCANCODE_BACKSLASH,		SDL_SCANCODE_Z,			SDL_SCANCODE_X,				SDL_SCANCODE_C,				SDL_SCANCODE_V,			/* 2 */

    SDL_SCANCODE_B,				SDL_SCANCODE_N,				SDL_SCANCODE_M,				SDL_SCANCODE_COMMA,			SDL_SCANCODE_PERIOD,	SDL_SCANCODE_SLASH,			SDL_SCANCODE_RSHIFT,		SDL_SCANCODE_PRINTSCREEN,/* 3 */
    SDL_SCANCODE_LALT,			SDL_SCANCODE_SPACE,			SDL_SCANCODE_CAPSLOCK,		SDL_SCANCODE_F1,			SDL_SCANCODE_F2,		SDL_SCANCODE_F3,			SDL_SCANCODE_F4,			SDL_SCANCODE_F5,		/* 3 */

    SDL_SCANCODE_F6,			SDL_SCANCODE_F7,			SDL_SCANCODE_F8,			SDL_SCANCODE_F9,			SDL_SCANCODE_F10,		SDL_SCANCODE_NUMLOCKCLEAR,	SDL_SCANCODE_SCROLLLOCK,	SDL_SCANCODE_HOME,		/* 4 */
    SDL_SCANCODE_UP,			SDL_SCANCODE_PAGEUP,		SDL_SCANCODE_KP_MINUS,		SDL_SCANCODE_LEFT,			SDL_SCANCODE_KP_5,		SDL_SCANCODE_RIGHT,			SDL_SCANCODE_KP_PLUS,		SDL_SCANCODE_END,		/* 4 */

    SDL_SCANCODE_DOWN,			SDL_SCANCODE_PAGEDOWN,		SDL_SCANCODE_INSERT,		SDL_SCANCODE_DELETE,		SDL_SCANCODE_UNKNOWN,	SDL_SCANCODE_UNKNOWN,		SDL_SCANCODE_NONUSBACKSLASH,SDL_SCANCODE_F11,		/* 5 */
    SDL_SCANCODE_F12,			SDL_SCANCODE_PAUSE,			SDL_SCANCODE_UNKNOWN,		SDL_SCANCODE_LGUI,			SDL_SCANCODE_RGUI,		SDL_SCANCODE_APPLICATION,	SDL_SCANCODE_UNKNOWN,		SDL_SCANCODE_UNKNOWN,	/* 5 */

    SDL_SCANCODE_UNKNOWN,		SDL_SCANCODE_UNKNOWN,		SDL_SCANCODE_UNKNOWN,		SDL_SCANCODE_UNKNOWN,		SDL_SCANCODE_F13,		SDL_SCANCODE_F14,			SDL_SCANCODE_F15,			SDL_SCANCODE_F16,		/* 6 */
    SDL_SCANCODE_F17,			SDL_SCANCODE_F18,			SDL_SCANCODE_F19,			SDL_SCANCODE_UNKNOWN,		SDL_SCANCODE_UNKNOWN,	SDL_SCANCODE_UNKNOWN,		SDL_SCANCODE_UNKNOWN,		SDL_SCANCODE_UNKNOWN,	/* 6 */

    SDL_SCANCODE_INTERNATIONAL2,		SDL_SCANCODE_UNKNOWN,		SDL_SCANCODE_UNKNOWN,		SDL_SCANCODE_INTERNATIONAL1,		SDL_SCANCODE_UNKNOWN,	SDL_SCANCODE_UNKNOWN,		SDL_SCANCODE_UNKNOWN,		SDL_SCANCODE_UNKNOWN,	/* 7 */
    SDL_SCANCODE_UNKNOWN,		SDL_SCANCODE_INTERNATIONAL4,		SDL_SCANCODE_UNKNOWN,		SDL_SCANCODE_INTERNATIONAL5,		SDL_SCANCODE_UNKNOWN,	SDL_SCANCODE_INTERNATIONAL3,		SDL_SCANCODE_UNKNOWN,		SDL_SCANCODE_UNKNOWN	/* 7 */
};

const SDL_Scancode scancode_rmapping_extended[][2] = {
    {SDL_SCANCODE_KP_ENTER, SDL_SCANCODE_RETURN},
    {SDL_SCANCODE_RALT, SDL_SCANCODE_LALT},
    {SDL_SCANCODE_RCTRL, SDL_SCANCODE_LCTRL},
    {SDL_SCANCODE_KP_DIVIDE, SDL_SCANCODE_SLASH},
    //{SDL_SCANCODE_KP_PLUS, SDL_SCANCODE_CAPSLOCK}
};

const SDL_Scancode scancode_rmapping_nonextended[][2] = {
    {SDL_SCANCODE_KP_7, SDL_SCANCODE_HOME},
    {SDL_SCANCODE_KP_8, SDL_SCANCODE_UP},
    {SDL_SCANCODE_KP_9, SDL_SCANCODE_PAGEUP},
    {SDL_SCANCODE_KP_4, SDL_SCANCODE_LEFT},
    {SDL_SCANCODE_KP_6, SDL_SCANCODE_RIGHT},
    {SDL_SCANCODE_KP_1, SDL_SCANCODE_END},
    {SDL_SCANCODE_KP_2, SDL_SCANCODE_DOWN},
    {SDL_SCANCODE_KP_3, SDL_SCANCODE_PAGEDOWN},
    {SDL_SCANCODE_KP_0, SDL_SCANCODE_INSERT},
    {SDL_SCANCODE_KP_PERIOD, SDL_SCANCODE_DELETE},
    {SDL_SCANCODE_KP_MULTIPLY, SDL_SCANCODE_PRINTSCREEN}
};

static void set_fullscreen(bool on, bool call_callback) {
#ifdef TARGET_IOS
    // iOS apps are always fullscreen
    return;
#endif
    if (fullscreen_state == on) {
        return;
    }
    fullscreen_state = on;

    if (on) {
        SDL_DisplayMode mode;
        SDL_GetDesktopDisplayMode(0, &mode);
        window_width = mode.w;
        window_height = mode.h;
    } else {
        window_width = DESIRED_SCREEN_WIDTH;
        window_height = DESIRED_SCREEN_HEIGHT;
    }
    SDL_SetWindowSize(wnd, window_width, window_height);
    SDL_SetWindowFullscreen(wnd, on ? SDL_WINDOW_FULLSCREEN : 0);

    if (on_fullscreen_changed_callback != NULL && call_callback) {
        on_fullscreen_changed_callback(on);
    }
}

#ifdef ENABLE_OPENGL
static void test_vsync(void) {
    // Even if SDL_GL_SetSwapInterval succeeds, it doesn't mean that VSync actually works.
    // A 60 Hz monitor should have a swap interval of 16.67 milliseconds.
    // Try to detect the length of a vsync by swapping buffers some times.
    // Since the graphics card may enqueue a fixed number of frames,
    // first send in four dummy frames to hopefully fill the queue.
    // This method will fail if the refresh rate is changed, which, in
    // combination with that we can't control the queue size (i.e. lag)
    // is a reason this generic SDL2 backend should only be used as last resort.
    Uint32 start;
    Uint32 end;

    SDL_GL_SwapWindow(wnd);
    SDL_GL_SwapWindow(wnd);
    SDL_GL_SwapWindow(wnd);
    SDL_GL_SwapWindow(wnd);
    SDL_GL_SwapWindow(wnd);
    SDL_GL_SwapWindow(wnd);
    SDL_GL_SwapWindow(wnd);
    SDL_GL_SwapWindow(wnd);
    start = SDL_GetTicks();
    SDL_GL_SwapWindow(wnd);
    SDL_GL_SwapWindow(wnd);
    SDL_GL_SwapWindow(wnd);
    SDL_GL_SwapWindow(wnd);
    end = SDL_GetTicks();

    float average = 4.0 * 1000.0 / (end - start);

    vsync_enabled = 1;
#ifdef HIGH_FPS_PC
    // Swap once per rendered sub-frame; the sub-frame count absorbs the
    // refresh rate (60 Hz -> 2, 90 Hz -> 3, 120 Hz -> 4)
    if (average > 27 && average < 33) {
        gMaxSubframes = 1;
    } else if (average > 57 && average < 63) {
        gMaxSubframes = 2;
    } else if (average > 86 && average < 94) {
        gMaxSubframes = 3;
    } else if (average > 115 && average < 125) {
        gMaxSubframes = 4;
    } else {
        // Unknown refresh rate: pace 60 fps with the timer
        vsync_enabled = 0;
        gMaxSubframes = 2;
    }
#else
    if (average > 27 && average < 33) {
        SDL_GL_SetSwapInterval(1);
    } else if (average > 57 && average < 63) {
        SDL_GL_SetSwapInterval(2);
    } else if (average > 86 && average < 94) {
        SDL_GL_SetSwapInterval(3);
    } else if (average > 115 && average < 125) {
        SDL_GL_SetSwapInterval(4);
    } else {
        vsync_enabled = 0;
    }
#endif
}
#endif

#ifdef ENABLE_METAL
static SDL_MetalView metal_view;

void *gfx_sdl_get_metal_layer(void) {
    return SDL_Metal_GetLayer(metal_view);
}
#endif

#ifdef TARGET_IOS
static void update_touch_geometry(void) {
    int width = 0;
    int height = 0;
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
    float view_width = 0.0f;
    float view_height = 0.0f;
    float pixels_per_point = 1.0f;

#ifdef ENABLE_METAL
    SDL_Metal_GetDrawableSize(wnd, &width, &height);
#else
    SDL_GL_GetDrawableSize(wnd, &width, &height);
#endif
    if (width <= 0 || height <= 0) {
        return;
    }
    if (SDL_iPhoneGetWindowSafeAreaInsets(wnd, &left, &top, &right, &bottom,
                                          &view_width, &view_height) != 0) {
        left = top = right = bottom = 0.0f;
    }
    if (view_width > 0.0f && view_height > 0.0f) {
        float scale_x = (float) width / view_width;
        float scale_y = (float) height / view_height;
        pixels_per_point = (scale_x + scale_y) * 0.5f;
    }
    window_width = (unsigned int) width;
    window_height = (unsigned int) height;
    touch_set_screen_geometry(width, height,
                              (int) (left * (float) width + 0.5f),
                              (int) (top * (float) height + 0.5f),
                              (int) (right * (float) width + 0.5f),
                              (int) (bottom * (float) height + 0.5f),
                              pixels_per_point);
}
#endif

static void gfx_sdl_init(const char *game_name, bool start_in_fullscreen) {
#ifdef TARGET_IOS
    SDL_SetHint(SDL_HINT_ORIENTATIONS,
                "LandscapeLeft LandscapeRight Portrait PortraitUpsideDown");
#endif
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        gfx_sdl_fatal("initializing SDL");
    }

#ifdef ENABLE_OPENGL
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    //SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    //SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);
#endif

    char title[512];
    snprintf(title, sizeof(title), "%s (%s)", game_name, GFX_API_NAME);

#ifdef ENABLE_METAL
    SDL_SetHint(SDL_HINT_IOS_HIDE_HOME_INDICATOR, "2");

    wnd = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            0, 0, SDL_WINDOW_METAL | SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN
            | SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALLOW_HIGHDPI);
    if (wnd == NULL) {
        gfx_sdl_fatal("creating the Metal window");
    }
    fullscreen_state = true;

    metal_view = SDL_Metal_CreateView(wnd);
    if (metal_view == NULL) {
        gfx_sdl_fatal("creating the Metal view");
    }

    // Render at the native (retina) resolution
    int drawable_w, drawable_h;
    SDL_Metal_GetDrawableSize(wnd, &drawable_w, &drawable_h);
    window_width = drawable_w;
    window_height = drawable_h;
#ifdef TARGET_IOS
    update_touch_geometry();
#endif

#ifdef HIGH_FPS_PC
    {
        // Interpolate up to the display refresh rate (60 Hz -> 2 sub-frames,
        // 120 Hz ProMotion -> 4)
        SDL_DisplayMode mode;
        if (SDL_GetCurrentDisplayMode(0, &mode) == 0 && mode.refresh_rate > 0
            && mode.refresh_rate % 30 == 0) {
            gMaxSubframes = mode.refresh_rate / 30;
        }
    }
#endif

    // Presentation pacing is handled by the Metal backend
    vsync_enabled = 1;
#elif defined(TARGET_IOS)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_SetHint(SDL_HINT_IOS_HIDE_HOME_INDICATOR, "2");

    wnd = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            0, 0, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN
            | SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALLOW_HIGHDPI);
    if (wnd == NULL) {
        gfx_sdl_fatal("creating the OpenGL ES window");
    }
    fullscreen_state = true;

    gl_context = SDL_GL_CreateContext(wnd);
    if (gl_context == NULL) {
        gfx_sdl_fatal("creating the OpenGL ES context");
    }

    // Render at the native (retina) resolution
    int drawable_w, drawable_h;
    SDL_GL_GetDrawableSize(wnd, &drawable_w, &drawable_h);
    window_width = drawable_w;
    window_height = drawable_h;
    update_touch_geometry();

    // Displays refresh at a multiple of the game's 30 fps; derive the swap
    // interval from the refresh rate instead of measuring it
    SDL_DisplayMode mode;
    int refresh_rate = 60;
    if (SDL_GetCurrentDisplayMode(0, &mode) == 0 && mode.refresh_rate > 0) {
        refresh_rate = mode.refresh_rate;
    }
    vsync_enabled = refresh_rate % 30 == 0;
    if (vsync_enabled) {
#ifdef HIGH_FPS_PC
        // One swap per rendered sub-frame
        gMaxSubframes = refresh_rate / 30;
        SDL_GL_SetSwapInterval(1);
#else
        SDL_GL_SetSwapInterval(refresh_rate / 30);
#endif
    }
#else
    wnd = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            window_width, window_height, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (wnd == NULL) {
        gfx_sdl_fatal("creating the OpenGL window");
    }

    if (start_in_fullscreen) {
        set_fullscreen(true, false);
    }

    gl_context = SDL_GL_CreateContext(wnd);
    if (gl_context == NULL) {
        gfx_sdl_fatal("creating the OpenGL context");
    }

    SDL_GL_SetSwapInterval(1);
    test_vsync();
#endif
    if (!vsync_enabled)
        puts("Warning: VSync is not enabled or not working. Falling back to timer for synchronization");

    for (size_t i = 0; i < sizeof(windows_scancode_table) / sizeof(SDL_Scancode); i++) {
        inverted_scancode_table[windows_scancode_table[i]] = i;
    }

    for (size_t i = 0; i < sizeof(scancode_rmapping_extended) / sizeof(scancode_rmapping_extended[0]); i++) {
        inverted_scancode_table[scancode_rmapping_extended[i][0]] = inverted_scancode_table[scancode_rmapping_extended[i][1]] + 0x100;
    }

    for (size_t i = 0; i < sizeof(scancode_rmapping_nonextended) / sizeof(scancode_rmapping_nonextended[0]); i++) {
        // This loop walks the non-extended table; indexing the extended one
        // here (as it used to) ran off the end of a four-element array
        inverted_scancode_table[scancode_rmapping_nonextended[i][0]] = inverted_scancode_table[scancode_rmapping_nonextended[i][1]];
        inverted_scancode_table[scancode_rmapping_nonextended[i][1]] += 0x100;
    }
}

static void gfx_sdl_set_fullscreen_changed_callback(void (*on_fullscreen_changed)(bool is_now_fullscreen)) {
    on_fullscreen_changed_callback = on_fullscreen_changed;
}

static void gfx_sdl_set_fullscreen(bool enable) {
    set_fullscreen(enable, true);
}

static void gfx_sdl_set_keyboard_callbacks(bool (*on_key_down)(int scancode), bool (*on_key_up)(int scancode), void (*on_all_keys_up)(void)) {
    on_key_down_callback = on_key_down;
    on_key_up_callback = on_key_up;
    on_all_keys_up_callback = on_all_keys_up;
}

static void gfx_sdl_main_loop(void (*run_one_game_iter)(void)) {
#ifdef TARGET_IOS
    // SDL's UIKit backend owns a CADisplayLink and calls this at the fastest
    // cadence the display currently makes available. Register the game runner
    // below; the callback keeps the fixed-rate game clock independent of that
    // variable presentation cadence.
    if (ios_run_one_game_iter == NULL) {
        ios_run_one_game_iter = run_one_game_iter;
        if (SDL_iPhoneSetAnimationCallback(wnd, 1, gfx_sdl_ios_animation_callback,
                                           NULL) < 0) {
            gfx_sdl_fatal("starting the iOS display callback");
        }
    }
#else
    while (1) {
        run_one_game_iter();
    }
#endif
}

#ifdef TARGET_IOS
static void SDLCALL gfx_sdl_ios_animation_callback(void *param) {
    static double next_logic_s;
    static bool event_pump_reenabled;
    (void) param;
    const double frequency = (double) SDL_GetPerformanceFrequency();
    const double now_s = (double) SDL_GetPerformanceCounter() / frequency;
#ifdef HIGH_FPS_PC
    const double logic_period_s = 1.0 / GAME_FRAMERATE;
#else
    const double logic_period_s = 1.0 / 30.0;
#endif

    // SDL disables its lifecycle observer after the app's main function
    // returns. The animation callback deliberately returns main to UIKit, so
    // restore that observer on the first display callback.
    if (!event_pump_reenabled) {
        SDL_iPhoneSetEventPump(SDL_TRUE);
        event_pump_reenabled = true;
    }

    // ProMotion is variable-rate: callbacks can arrive at 120, 80, 60, or a
    // lower system-selected cadence. Advance game logic from real time rather
    // than every N callbacks so a refresh-rate change cannot slow gameplay or
    // audio. Never run catch-up ticks in a burst after a suspension or hitch.
    if (next_logic_s == 0.0 || now_s - next_logic_s >= 0.2) {
        next_logic_s = now_s;
    }
    if (now_s + 0.0005 < next_logic_s) {
        return;
    }

    ios_run_one_game_iter();
    next_logic_s += logic_period_s;
    if (next_logic_s <= now_s) {
        next_logic_s = now_s + logic_period_s;
    }
}
#endif

static void gfx_sdl_get_dimensions(uint32_t *width, uint32_t *height) {
#ifdef ENABLE_METAL
    // Query live so retro mode's low-resolution drawable is reflected
    int w = 0, h = 0;
    SDL_Metal_GetDrawableSize(wnd, &w, &h);
    if (w > 0 && h > 0) {
        *width = w;
        *height = h;
        return;
    }
#endif
    *width = window_width;
    *height = window_height;
}

static int translate_scancode(int scancode) {
    if (scancode < 512) {
        return inverted_scancode_table[scancode];
    } else {
        return 0;
    }
}

static void gfx_sdl_onkeydown(int scancode) {
    int key = translate_scancode(scancode);
    if (on_key_down_callback != NULL) {
        on_key_down_callback(key);
    }
}

static void gfx_sdl_onkeyup(int scancode) {
    int key = translate_scancode(scancode);
    if (on_key_up_callback != NULL) {
        on_key_up_callback(key);
    }
}

// Persists everything the process owns that is not already on disk. The
// system can kill a suspended app without any further notice, so this runs
// before backgrounding rather than from an atexit handler, which iOS never
// gets around to calling.
static void save_state_before_suspend(void) {
    configfile_save_current();
#ifdef TARGET_IOS
    // No-op unless the player rearranged the on-screen controls
    touch_layout_save();
#endif
}

static void quit_now(void) {
    save_state_before_suspend();
    exit(0);
}

// Parks the game loop while the app is in the background. Submitting GPU
// work while suspended gets the app killed by the system, and spinning the
// loop drains the battery for a game nobody is looking at, so block on the
// event queue (which keeps pumping the platform run loop) until the system
// brings us back.
//
// Everything else delivered while parked is consumed by this loop and never
// reaches the normal handler, so anything it would have updated has to be
// re-established on the way out rather than assumed unchanged.
static void wait_for_foreground(void) {
    SDL_Event event;

#ifdef TARGET_IOS
    ios_audio_session_set_app_active(false);
#endif

    while (SDL_WaitEvent(&event)) {
        if (event.type == SDL_APP_WILLENTERFOREGROUND || event.type == SDL_APP_DIDENTERFOREGROUND) {
            break;
        }
        if (event.type == SDL_APP_TERMINATING || event.type == SDL_QUIT) {
            quit_now();
        }
    }

#ifdef TARGET_IOS
    ios_audio_session_set_app_active(true);

    // Touch-cancel and finger-up events raised as iOS took the touch stream
    // away were consumed above, so any finger still recorded as down would
    // stay down forever, holding its button
    touch_forget_fingers();

    // A rotation while suspended raises resize events into the discarded
    // stream; re-read the drawable and UIKit safe area instead.
    update_touch_geometry();
#endif
#ifdef HIGH_FPS_PC
    // Frame timings measured either side of a suspension say nothing about
    // how well the device is keeping up
    framerate_reset();
#endif
}

static void gfx_sdl_handle_events(void) {
    SDL_Event event;
#ifdef TARGET_IOS
    // safeAreaInsetsDidChange can occur without a drawable-size event. The
    // query is inexpensive and the touch layer ignores unchanged geometry.
    update_touch_geometry();
#endif
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_APP_WILLENTERBACKGROUND:
                save_state_before_suspend();
                break;
            case SDL_APP_DIDENTERBACKGROUND:
                wait_for_foreground();
                break;
            case SDL_APP_TERMINATING:
                quit_now();
                break;
            case SDL_APP_LOWMEMORY:
            case SDL_APP_WILLENTERFOREGROUND:
            case SDL_APP_DIDENTERFOREGROUND:
                break;
#ifndef TARGET_WEB
            // Scancodes are broken in Emscripten SDL2: https://bugzilla.libsdl.org/show_bug.cgi?id=3259
            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_F10) {
                    set_fullscreen(!fullscreen_state, true);
                    break;
                }
                gfx_sdl_onkeydown(event.key.keysym.scancode);
                break;
            case SDL_KEYUP:
                gfx_sdl_onkeyup(event.key.keysym.scancode);
                break;
#endif
#ifdef TARGET_IOS
            case SDL_FINGERDOWN:
                touch_down(event.tfinger.fingerId, event.tfinger.x, event.tfinger.y);
                break;
            case SDL_FINGERMOTION:
                touch_motion(event.tfinger.fingerId, event.tfinger.x, event.tfinger.y);
                break;
            case SDL_FINGERUP:
                touch_up(event.tfinger.fingerId);
                break;
#endif
            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED
                    || event.window.event == SDL_WINDOWEVENT_RESIZED) {
#ifdef TARGET_IOS
                    update_touch_geometry();
#elif defined(ENABLE_METAL)
                    SDL_Metal_GetDrawableSize(wnd, (int *) &window_width, (int *) &window_height);
#else
                    window_width = event.window.data1;
                    window_height = event.window.data2;
#endif
                }
                break;
            case SDL_QUIT:
                quit_now();
        }
    }
}

static bool gfx_sdl_start_frame(void) {
    return true;
}

#ifndef ENABLE_METAL
static void sync_framerate_with_timer(void) {
    // Number of milliseconds a rendered frame should take
#ifdef HIGH_FPS_PC
    Uint32 frame_time = 1000 / (GAME_FRAMERATE * gRenderSubframes);
#else
    const Uint32 frame_time = 1000 / 30; // HIGH_FPS off: vanilla 30 fps pacing
#endif
    static Uint32 last_time;
    Uint32 elapsed = SDL_GetTicks() - last_time;

    if (elapsed < frame_time)
        SDL_Delay(frame_time - elapsed);
    last_time += frame_time;
}
#endif

static void gfx_sdl_swap_buffers_begin(void) {
#ifdef ENABLE_METAL
    // The Metal backend schedules the present and paces the frame rate
    gfx_metal_present();
#else
#ifdef TARGET_IOS
    touch_render_overlay(window_width, window_height);
#endif

#ifdef HIGH_FPS_PC
    {
        // The frame cap can change the sub-frame count at runtime; keep the
        // swap interval matched so vsync pacing stays correct
        static int last_applied_subframes;
        if (vsync_enabled && gMaxSubframes > 0 && last_applied_subframes != gRenderSubframes) {
            int interval = gMaxSubframes / gRenderSubframes;
            last_applied_subframes = gRenderSubframes;
            SDL_GL_SetSwapInterval(interval < 1 ? 1 : interval);
        }
    }
#endif

    if (!vsync_enabled) {
        sync_framerate_with_timer();
    }

    SDL_GL_SwapWindow(wnd);
#endif
}

static void gfx_sdl_swap_buffers_end(void) {
}

static double gfx_sdl_get_time(void) {
    return 0.0;
}

struct GfxWindowManagerAPI gfx_sdl = {
    gfx_sdl_init,
    gfx_sdl_set_keyboard_callbacks,
    gfx_sdl_set_fullscreen_changed_callback,
    gfx_sdl_set_fullscreen,
    gfx_sdl_main_loop,
    gfx_sdl_get_dimensions,
    gfx_sdl_handle_events,
    gfx_sdl_start_frame,
    gfx_sdl_swap_buffers_begin,
    gfx_sdl_swap_buffers_end,
    gfx_sdl_get_time
};

#endif
