#import <AVFoundation/AVFoundation.h>
#import <UIKit/UIKit.h>

#include <SDL2/SDL.h>

#include "ios_support.h"
#include "audio/audio_sdl.h"
#include "controller/controller_touch.h"

extern "C" {
#include "configfile.h"
#include "framerate.h"
#include "game/options_menu.h"
}

static bool platform_initialized;
static bool app_active;
static id route_observer;
static id media_reset_observer;
static id scene_will_deactivate_observer;
static id scene_did_enter_background_observer;
static id scene_will_enter_foreground_observer;
static id scene_did_activate_observer;
static id thermal_observer;
static id power_state_observer;
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

void ios_run_on_main_queue_async(void (*work)(void)) {
    if (work == NULL) {
        return;
    }
    dispatch_async(dispatch_get_main_queue(), ^{
        work();
    });
}

// Translates what iOS reports about the device into a sub-frame ceiling.
//
// The measured backoff in framerate.c only steps down after frames have
// already been late, which the player feels. iOS announces thermal pressure
// before it throttles and Low Power Mode the moment the user asks for it, so
// both are better acted on than waited for. Fair state is left alone: it is
// common under normal play and is not a signal to give up half the frame rate.
static void update_platform_frame_ceiling(void) {
#ifdef HIGH_FPS_PC
    NSProcessInfo *info = [NSProcessInfo processInfo];
    s32 ceiling = MAX_SUBFRAMES;

    switch (info.thermalState) {
        case NSProcessInfoThermalStateSerious:
            ceiling = 2; // 60 fps
            break;
        case NSProcessInfoThermalStateCritical:
            ceiling = 1; // 30 fps, the native logic rate
            break;
        default:
            break;
    }
    if (info.lowPowerModeEnabled && ceiling > 2) {
        ceiling = 2;
    }
    framerate_set_platform_ceiling(ceiling);
#endif
}

void ios_audio_session_set_app_active(bool active) {
    if (!platform_initialized) {
        return;
    }
    audio_sdl_pause(!active);
}

bool ios_platform_is_app_active(void) {
    return app_active;
}

static void scene_will_deactivate(void) {
    app_active = false;

    // A suspended process can be terminated without another callback. Persist
    // immediately from the scene transition instead of waiting for SDL's event
    // queue to be drained by a future display refresh.
    configfile_save_current();
    touch_layout_save();
    audio_sdl_pause(true);
}

static void scene_will_enter_foreground(void) {
    touch_forget_fingers();
#ifdef HIGH_FPS_PC
    framerate_resume();
#endif
    fps_counter_reset();
}

static void scene_did_activate(void) {
    app_active = true;
    touch_forget_fingers();
#ifdef HIGH_FPS_PC
    framerate_resume();
#endif
    fps_counter_reset();
    audio_sdl_pause(false);
}

void ios_platform_init(void) {
    if (platform_initialized) {
        return;
    }
    platform_initialized = true;
    app_active = UIApplication.sharedApplication.applicationState == UIApplicationStateActive;

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
        audio_sdl_media_services_reset();
    }];

    scene_will_deactivate_observer = [center
        addObserverForName:UISceneWillDeactivateNotification
                    object:nil
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(__unused NSNotification *note) {
        scene_will_deactivate();
    }];

    scene_did_enter_background_observer = [center
        addObserverForName:UISceneDidEnterBackgroundNotification
                    object:nil
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(__unused NSNotification *note) {
        scene_will_deactivate();
    }];

    scene_will_enter_foreground_observer = [center
        addObserverForName:UISceneWillEnterForegroundNotification
                    object:nil
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(__unused NSNotification *note) {
        scene_will_enter_foreground();
    }];

    scene_did_activate_observer = [center
        addObserverForName:UISceneDidActivateNotification
                    object:nil
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(__unused NSNotification *note) {
        scene_did_activate();
    }];

    // Both are delivered on an unspecified queue; the main queue is where the
    // frame pacing reads the ceiling.
    thermal_observer = [center
        addObserverForName:NSProcessInfoThermalStateDidChangeNotification
                    object:nil
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(__unused NSNotification *note) {
        update_platform_frame_ceiling();
    }];

    power_state_observer = [center
        addObserverForName:NSProcessInfoPowerStateDidChangeNotification
                    object:nil
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(__unused NSNotification *note) {
        update_platform_frame_ceiling();
    }];

    // The device may already be warm or in Low Power Mode at launch
    update_platform_frame_ceiling();
}
