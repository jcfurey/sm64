#import <AVFoundation/AVFoundation.h>
#import <UIKit/UIKit.h>

#include <SDL2/SDL.h>

#include "ios_support.h"
#include "audio/audio_sdl.h"
#include "controller/controller_touch.h"

static bool platform_initialized;
static id route_observer;
static id media_reset_observer;
static UIImpactFeedbackGenerator *touch_feedback;

static void fire_touch_haptic(void) {
    void (^feedback)(void) = ^{
        if (touch_feedback == nil) {
            touch_feedback = [[UIImpactFeedbackGenerator alloc]
                initWithStyle:UIImpactFeedbackStyleLight];
        }
        [touch_feedback impactOccurred];
        [touch_feedback prepare];
    };

    if ([NSThread isMainThread]) {
        feedback();
    } else {
        dispatch_async(dispatch_get_main_queue(), feedback);
    }
}

void ios_audio_session_set_app_active(bool active) {
    if (!platform_initialized) {
        return;
    }
    audio_sdl_pause(!active);
}

void ios_platform_init(void) {
    if (platform_initialized) {
        return;
    }
    platform_initialized = true;

    // SDL's CoreAudio backend owns AVAudioSession activation and interruption
    // handling. Tell that single owner which category to use before it opens
    // the audio device; independently changing the same session here causes
    // competing pause/resume transitions and audible startup glitches.
    SDL_SetHint(SDL_HINT_AUDIO_CATEGORY, "AVAudioSessionCategoryAmbient");
    touch_set_haptic_callback(fire_touch_haptic);

    NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
    route_observer = [center
        addObserverForName:AVAudioSessionRouteChangeNotification
                    object:nil
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(__unused NSNotification *note) {
        audio_sdl_route_changed();
    }];

    media_reset_observer = [center
        addObserverForName:AVAudioSessionMediaServicesWereResetNotification
                    object:nil
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(__unused NSNotification *note) {
        audio_sdl_route_changed();
    }];
}
