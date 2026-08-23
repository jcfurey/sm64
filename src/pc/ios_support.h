#ifndef IOS_SUPPORT_H
#define IOS_SUPPORT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initializes the UIKit-only services after the UIWindowScene is connected.
void ios_platform_init(void);

// The display callback checks this scene-owned state before touching game or
// Metal objects. It changes synchronously with UIScene lifecycle callbacks.
bool ios_platform_is_app_active(void);

// Keeps AVAudioSession and SDL's queued device in sync with app visibility.
void ios_audio_session_set_app_active(bool active);

#ifdef __cplusplus
}
#endif

#endif
