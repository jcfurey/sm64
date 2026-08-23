#ifndef AUDIO_SDL_H
#define AUDIO_SDL_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

extern struct AudioAPI audio_sdl;

// Suspends and resumes the audio device around app backgrounding. Safe to
// call before init or when another backend is in use.
void audio_sdl_pause(bool pause);

// Drops samples queued for an old output route. SDL keeps the device itself
// alive while iOS switches between speakers, headphones, and Bluetooth.
void audio_sdl_route_changed(void);

// Recreates the CoreAudio-backed SDL device after mediaserverd resets. Apple
// invalidates existing audio queues in this transition; clearing is not enough.
void audio_sdl_media_services_reset(void);

#ifdef __cplusplus
}
#endif

#endif
