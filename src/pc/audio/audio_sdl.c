#if !defined(_WIN32) && !defined(_WIN64)

#ifdef __MINGW32__
#include "SDL.h"
#else
#include "SDL2/SDL.h"
#endif

#ifdef TARGET_IOS
#include <TargetConditionals.h>
#endif

#include "audio_api.h"

static SDL_AudioDeviceID dev;

static bool audio_sdl_init(void) {
    if (SDL_Init(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL init error: %s\n", SDL_GetError());
        return false;
    }
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = 32000;
    want.format = AUDIO_S16;
    want.channels = 2;
#if defined(TARGET_IOS) && TARGET_OS_SIMULATOR
    // CoreSimulator proxies audio through macOS. Give that extra hop more
    // scheduling headroom so its real-time I/O thread does not miss cycles.
    want.samples = 1024;
#else
    want.samples = 512;
#endif
    want.callback = NULL;
    dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (dev == 0) {
        fprintf(stderr, "SDL_OpenAudio error: %s\n", SDL_GetError());
        return false;
    }
    SDL_PauseAudioDevice(dev, 0);
    return true;
}

static int audio_sdl_buffered(void) {
    return SDL_GetQueuedAudioSize(dev) / 4;
}

static int audio_sdl_get_desired_buffered(void) {
    return 1100;
}

static void audio_sdl_play(const uint8_t *buf, size_t len) {
    if (audio_sdl_buffered() < 6000) {
        // Don't fill the audio buffer too much in case this happens
        SDL_QueueAudio(dev, buf, len);
    }
}

// Stops the device while the app is suspended and discards the backlog on
// the way back, so resuming does not replay stale audio or spend the first
// seconds dropping samples because the queue looks full
void audio_sdl_pause(bool pause) {
    if (dev == 0) {
        return;
    }
    if (pause) {
        SDL_PauseAudioDevice(dev, 1);
    } else {
        SDL_ClearQueuedAudio(dev);
        SDL_PauseAudioDevice(dev, 0);
    }
}

struct AudioAPI audio_sdl = {
    audio_sdl_init,
    audio_sdl_buffered,
    audio_sdl_get_desired_buffered,
    audio_sdl_play
};

#endif
