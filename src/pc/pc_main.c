#include <stdlib.h>
#include <time.h>

#ifdef TARGET_WEB
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#ifdef TARGET_IOS
// SDL provides the UIKit application entry point and redefines main below
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

#include "game/game_init.h" // for gGlobalTimer
void exec_display_list(struct SPTask *spTask) {
    if (!inited) {
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

// Rendered frames per second, measured over the last second; displayed by
// the in-game FPS counter
s32 gCurrentFPS = 0;

static s32 sFPSAccum;
static long long sFPSWindowStartMs;

static void fps_count_frames(s32 frames) {
    long long now = framerate_monotonic_ms();
    sFPSAccum += frames;
    if (sFPSWindowStartMs == 0) {
        sFPSWindowStartMs = now;
    } else if (now - sFPSWindowStartMs >= 1000) {
        gCurrentFPS = (s32)(sFPSAccum * 1000 / (now - sFPSWindowStartMs));
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

void produce_one_frame(void) {
    gfx_start_frame();
#ifdef HIGH_FPS_PC
    framerate_note_logic_frame(framerate_monotonic_ms());
    gRenderSubframes = framerate_choose_subframes();
    patch_interpolations_reset();
#endif
    game_loop_one_iteration();
    
    int samples_left = audio_api->buffered();
    u32 num_audio_samples = samples_left < audio_api->get_desired_buffered() ? SAMPLES_HIGH : SAMPLES_LOW;
    //printf("Audio samples: %d %u\n", samples_left, num_audio_samples);
    s16 audio_buffer[SAMPLES_HIGH * 2 * 2];
    for (int i = 0; i < 2; i++) {
        /*if (audio_cnt-- == 0) {
            audio_cnt = 2;
        }
        u32 num_audio_samples = audio_cnt < 2 ? 528 : 544;*/
        create_next_audio_buffer(audio_buffer + i * (num_audio_samples * 2), num_audio_samples);
    }
    //printf("Audio samples before submitting: %d\n", audio_api->buffered());
    audio_api->play((u8 *)audio_buffer, 2 * num_audio_samples * 4);
    
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

    gShowDebugText = configDebugInfo;
    gShowProfiler = configDebugInfo;
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
    while (1) {
        wm_api->main_loop(produce_one_frame);
    }
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
