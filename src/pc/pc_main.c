#include <stdlib.h>
#include <time.h>

#ifdef TARGET_WEB
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#ifdef TARGET_IOS
// SDL provides the UIKit application entry point and redefines main below
#include <TargetConditionals.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_main.h>
#endif

#include "sm64.h"

#include "game/memory.h"
#include "audio/external.h"

#include "gfx/gfx_pc.h"
#include "gfx/gfx_opengl.h"
#include "gfx/gfx_metal.h"
#include "gfx/gfx_direct3d11.h"
#include "gfx/gfx_direct3d12.h"
#include "gfx/gfx_dxgi.h"
#include "gfx/gfx_glx.h"
#include "gfx/gfx_sdl.h"
#include "gfx/gfx_dummy.h"

#include "audio/audio_api.h"
#include "audio/audio_wasapi.h"
#include "audio/audio_pulse.h"
#include "audio/audio_alsa.h"
#include "audio/audio_sdl.h"
#include "audio/audio_null.h"

#include "controller/controller_keyboard.h"

#include "configfile.h"

#include "compat.h"
#include "fs.h"
#include "framerate.h"

#ifdef TARGET_IOS
#include "ios_support.h"
#endif

#define CONFIG_FILE "sm64config.txt"

OSMesg gMainReceivedMesg;
OSMesgQueue gSIEventMesgQueue;

s8 gResetTimer;
s8 gNmiResetBarsTimer;
s8 gDebugLevelSelect;
s8 gShowProfiler;
s8 gShowDebugText;

static struct AudioAPI *audio_api;
static struct GfxWindowManagerAPI *wm_api;
static struct GfxRenderingAPI *rendering_api;

extern void gfx_run(Gfx *commands);
extern void thread5_game_loop(void *arg);
extern void create_next_audio_buffer(s16 *samples, u32 num_samples);
void game_loop_one_iteration(void);

void dispatch_audio_sptask(UNUSED struct SPTask *spTask) {
}

void set_vblank_handler(UNUSED s32 index, UNUSED struct VblankHandler *handler, UNUSED OSMesgQueue *queue, UNUSED OSMesg *msg) {
}

static uint8_t inited = 0;
static bool sRenderSuppressed;

#include "game/game_init.h" // for gGlobalTimer
void exec_display_list(struct SPTask *spTask) {
    if (!inited || sRenderSuppressed) {
        return;
    }
    gfx_run((Gfx *)spTask->task.t.data_ptr);
}

#define printf

#ifdef VERSION_EU
#define SAMPLES_HIGH 656
#define SAMPLES_LOW 640
#else
#define SAMPLES_HIGH 544
#define SAMPLES_LOW 528
#endif

// Frames that actually reached the display per second over the last second;
// displayed by the in-game FPS counter. Other backends count completed frame
// submissions, while iOS Metal supplies presentation-confirmed frames.
s32 gCurrentFPS = 0;

static s32 sFPSAccum;
static long long sFPSWindowStartMs;
#if defined(TARGET_IOS) && defined(ENABLE_METAL) && !TARGET_OS_SIMULATOR
static u64 sFPSLastPresentedFrames;
#endif

void fps_counter_reset(void) {
    gCurrentFPS = 0;
    sFPSAccum = 0;
    sFPSWindowStartMs = 0;
#if defined(TARGET_IOS) && defined(ENABLE_METAL) && !TARGET_OS_SIMULATOR
    sFPSLastPresentedFrames = gfx_metal_presented_frame_count();
#endif
}

static void fps_count_frames(s32 frames) {
    long long now = framerate_monotonic_ms();
#if defined(TARGET_IOS) && defined(ENABLE_METAL) && !TARGET_OS_SIMULATOR
    u64 presented_frames = gfx_metal_presented_frame_count();

    frames = (s32) (presented_frames - sFPSLastPresentedFrames);
    sFPSLastPresentedFrames = presented_frames;
#endif
    sFPSAccum += frames;
    if (sFPSWindowStartMs == 0) {
        sFPSWindowStartMs = now;
    } else if (now - sFPSWindowStartMs >= 1000) {
        gCurrentFPS = (s32)(sFPSAccum * 1000 / (now - sFPSWindowStartMs));
#ifdef HIGH_FPS_PC
        framerate_note_presented_rate(gCurrentFPS, GAME_FRAMERATE * gRenderSubframes);
#endif
        sFPSAccum = 0;
        sFPSWindowStartMs = now;
    }
}

void configfile_save_current(void) {
    configfile_save(fs_get_write_path(CONFIG_FILE));
}

#ifdef HIGH_FPS_PC
// Invalidates all recorded interpolation patch positions; called before
// each game logic frame so stale positions can never be rewritten
static void patch_interpolations_reset(void) {
    extern void mtx_patch_interpolated_reset(void);
    extern void patch_screen_transition_reset(void);
    extern void patch_title_screen_scales_reset(void);
    extern void patch_interpolated_dialog_reset(void);
    extern void patch_interpolated_hud_reset(void);
    extern void patch_interpolated_paintings_reset(void);
    mtx_patch_interpolated_reset();
    patch_screen_transition_reset();
    patch_title_screen_scales_reset();
    patch_interpolated_dialog_reset();
    patch_interpolated_hud_reset();
    patch_interpolated_paintings_reset();
}

// Rewrites the display list for render variant v (see framerate.h)
static void patch_interpolations(s32 v) {
    extern void mtx_patch_interpolated(s32 v);
    extern void patch_screen_transition_interpolated(s32 v);
    extern void patch_title_screen_scales(s32 v);
    extern void patch_interpolated_dialog(s32 v);
    extern void patch_interpolated_hud(s32 v);
    extern void patch_interpolated_paintings(s32 v);
    extern void patch_interpolated_bubble_particles(s32 v);
    extern void patch_interpolated_snow_particles(s32 v);
    mtx_patch_interpolated(v);
    patch_screen_transition_interpolated(v);
    patch_title_screen_scales(v);
    patch_interpolated_dialog(v);
    patch_interpolated_hud(v);
    patch_interpolated_paintings(v);
    patch_interpolated_bubble_particles(v);
    patch_interpolated_snow_particles(v);
}
#endif

static void produce_audio_for_logic_tick(void) {
    int samples_left = audio_api->buffered();
    u32 num_audio_samples = samples_left < audio_api->get_desired_buffered() ? SAMPLES_HIGH : SAMPLES_LOW;
    s16 audio_buffer[SAMPLES_HIGH * 2 * 2];

    for (int i = 0; i < 2; i++) {
        create_next_audio_buffer(audio_buffer + i * (num_audio_samples * 2), num_audio_samples);
    }
    audio_api->play((u8 *)audio_buffer, 2 * num_audio_samples * 4);
}

#ifdef TARGET_IOS
// iOS receives a callback for every display refresh. Game logic and audio use
// their own real-time 30 Hz deadline, while each callback submits at most one
// drawable. A bounded catch-up preserves game time if ProMotion temporarily
// chooses a rate below 30 Hz without replaying an unbounded suspension gap.
static void produce_one_frame(void) {
    static double next_logic_s;
    static double next_render_s;
    static s32 render_variant;
    static s32 previous_subframes;
    const double frequency = (double) SDL_GetPerformanceFrequency();
    const double now_s = (double) SDL_GetPerformanceCounter() / frequency;
    const double logic_period_s = 1.0 / GAME_FRAMERATE;
    const s32 max_logic_catchup = 4;
    s32 logic_ticks = 0;

    // Poll input and refresh safe-area/drawable geometry even on callbacks
    // skipped by a lower frame cap. This does not acquire a Metal drawable.
    gfx_start_frame();

    if (next_logic_s == 0.0 || now_s - next_logic_s >= 0.25) {
        next_logic_s = now_s;
        next_render_s = now_s;
    }

    while (now_s + 0.0005 >= next_logic_s && logic_ticks < max_logic_catchup) {
#ifdef HIGH_FPS_PC
        framerate_note_logic_frame(framerate_monotonic_ms());
        gRenderSubframes = framerate_choose_subframes();
        patch_interpolations_reset();
#endif

        // The simulation builds the next display list, but presentation is
        // owned by this display callback below. This is what prevents a single
        // 30 Hz tick from acquiring four CAMetalLayer drawables in a batch.
        sRenderSuppressed = true;
        game_loop_one_iteration();
        sRenderSuppressed = false;
        produce_audio_for_logic_tick();

        render_variant = 0;
        logic_ticks++;
        next_logic_s += logic_period_s;
    }

    if (next_logic_s <= now_s) {
        // Four ticks cover the lowest 10 Hz cadence supported by ProMotion.
        // Anything still behind is a discontinuity, not useful catch-up work.
        next_logic_s = now_s + logic_period_s;
    }

    if (gGfxSPTask == NULL) {
        return;
    }

#ifdef HIGH_FPS_PC
    {
        const double render_period_s = logic_period_s / gRenderSubframes;

        if (next_render_s == 0.0 || previous_subframes != gRenderSubframes
            || next_render_s < now_s - render_period_s) {
            next_render_s = now_s;
        }
        previous_subframes = gRenderSubframes;
        if (now_s + 0.0005 < next_render_s) {
            return;
        }

        if (render_variant >= gRenderSubframes) {
            render_variant = gRenderSubframes - 1;
        }
        patch_interpolations(render_variant);
        exec_display_list(gGfxSPTask);
        gfx_end_frame();
        fps_count_frames(1);

        if (render_variant + 1 < gRenderSubframes) {
            render_variant++;
        }
        next_render_s += render_period_s;
        if (next_render_s <= now_s) {
            next_render_s = now_s + render_period_s;
        }
    }
#else
    // The non-interpolated build presents only when it advanced the 30 Hz
    // simulation; intermediate display callbacks exist solely to pump events.
    if (logic_ticks == 0) {
        return;
    }
    exec_display_list(gGfxSPTask);
    gfx_end_frame();
    fps_count_frames(1);
#endif
}
#else
void produce_one_frame(void) {
    gfx_start_frame();
#ifdef HIGH_FPS_PC
    framerate_note_logic_frame(framerate_monotonic_ms());
    gRenderSubframes = framerate_choose_subframes();
    patch_interpolations_reset();
#endif
    game_loop_one_iteration();
    produce_audio_for_logic_tick();
    gfx_end_frame();

#ifdef HIGH_FPS_PC
    for (s32 v = 1; v < gRenderSubframes; v++) {
        gfx_start_frame();
        patch_interpolations(v);
        exec_display_list(gGfxSPTask);
        gfx_end_frame();
    }
    fps_count_frames(gRenderSubframes);
#else
    fps_count_frames(1);
#endif
}
#endif

#ifdef TARGET_WEB
static void em_main_loop(void) {
}

static void request_anim_frame(void (*func)(double time)) {
    EM_ASM(requestAnimationFrame(function(time) {
        dynCall("vd", $0, [time]);
    }), func);
}

static void on_anim_frame(double time) {
    static double target_time;

    time *= 0.03; // milliseconds to frame count (33.333 ms -> 1)

    if (time >= target_time + 10.0) {
        // We are lagging 10 frames behind, probably due to coming back after inactivity,
        // so reset, with a small margin to avoid potential jitter later.
        target_time = time - 0.010;
    }

    for (int i = 0; i < 2; i++) {
        // If refresh rate is 15 Hz or something we might need to generate two frames
        if (time >= target_time) {
            produce_one_frame();
            target_time = target_time + 1.0;
        }
    }

    request_anim_frame(on_anim_frame);
}
#endif

static void save_config(void) {
    configfile_save(fs_get_write_path(CONFIG_FILE));
}

static void on_fullscreen_changed(bool is_now_fullscreen) {
    configFullscreen = is_now_fullscreen;
}

void main_func(void) {
#ifdef USE_SYSTEM_MALLOC
    main_pool_init();
    gGfxAllocOnlyPool = alloc_only_pool_init();
#else
    static u64 pool[0x165000/8 / 4 * sizeof(void *)];
    main_pool_init(pool, pool + sizeof(pool) / sizeof(pool[0]));
#endif
    gEffectsMemoryPool = mem_pool_init(0x4000, MEMORY_POOL_LEFT);

    configfile_load(fs_get_write_path(CONFIG_FILE));
    atexit(save_config);

#ifdef TARGET_IOS
    ios_platform_init();
#endif

    gShowDebugText = configDebugInfo;
    // The profiler depends on osGetTime(), which this port stubs to zero.
    // Keep it off even when the working debug-text overlay is persisted.
    gShowProfiler = FALSE;
    gDebugLevelSelect = configLevelSelect;

#ifdef TARGET_WEB
    emscripten_set_main_loop(em_main_loop, 0, 0);
    request_anim_frame(on_anim_frame);
#endif

#if defined(ENABLE_DX12)
    rendering_api = &gfx_direct3d12_api;
    wm_api = &gfx_dxgi_api;
#elif defined(ENABLE_DX11)
    rendering_api = &gfx_direct3d11_api;
    wm_api = &gfx_dxgi_api;
#elif defined(ENABLE_METAL)
    rendering_api = &gfx_metal_api;
    wm_api = &gfx_sdl;
#elif defined(ENABLE_OPENGL)
    rendering_api = &gfx_opengl_api;
    #if defined(__linux__) || defined(__BSD__)
        wm_api = &gfx_glx;
    #else
        wm_api = &gfx_sdl;
    #endif
#elif defined(ENABLE_GFX_DUMMY)
    rendering_api = &gfx_dummy_renderer_api;
    wm_api = &gfx_dummy_wm_api;
#endif

    gfx_init(wm_api, rendering_api, "Super Mario 64 PC-Port", configFullscreen);
    
    wm_api->set_fullscreen_changed_callback(on_fullscreen_changed);
    wm_api->set_keyboard_callbacks(keyboard_on_key_down, keyboard_on_key_up, keyboard_on_all_keys_up);
    
#if HAVE_WASAPI
    if (audio_api == NULL && audio_wasapi.init()) {
        audio_api = &audio_wasapi;
    }
#endif
#if HAVE_PULSE_AUDIO
    if (audio_api == NULL && audio_pulse.init()) {
        audio_api = &audio_pulse;
    }
#endif
#if HAVE_ALSA
    if (audio_api == NULL && audio_alsa.init()) {
        audio_api = &audio_alsa;
    }
#endif
#if defined(TARGET_WEB) || defined(TARGET_IOS)
    if (audio_api == NULL && audio_sdl.init()) {
        audio_api = &audio_sdl;
    }
#endif
    if (audio_api == NULL) {
        audio_api = &audio_null;
    }

    audio_init();
    sound_init();

    thread5_game_loop(NULL);
#ifdef TARGET_WEB
    /*for (int i = 0; i < atoi(argv[1]); i++) {
        game_loop_one_iteration();
    }*/
    inited = 1;
#else
    inited = 1;
#ifdef TARGET_IOS
    // UIKit must regain its run loop after SDL installs the display callback.
    // The callback drives fixed-rate logic and variable-rate presentation.
    wm_api->main_loop(produce_one_frame);
#else
    while (1) {
        wm_api->main_loop(produce_one_frame);
    }
#endif
#endif
}

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
int WINAPI WinMain(UNUSED HINSTANCE hInstance, UNUSED HINSTANCE hPrevInstance, UNUSED LPSTR pCmdLine, UNUSED int nCmdShow) {
    main_func();
    return 0;
}
#else
int main(UNUSED int argc, UNUSED char *argv[]) {
    main_func();
    return 0;
}
#endif
