#ifndef AUDIO_SDL_H
#define AUDIO_SDL_H

#include <stdbool.h>

extern struct AudioAPI audio_sdl;

// Suspends and resumes the audio device around app backgrounding. Safe to
// call before init or when another backend is in use.
void audio_sdl_pause(bool pause);

#endif
