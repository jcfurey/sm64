#import <AVFoundation/AVFoundation.h>
#import <UIKit/UIKit.h>

#include <stdio.h>

#include "ios_support.h"
#include "audio/audio_sdl.h"
#include "audio/audio_session_policy.h"
#include "controller/controller_touch.h"

static struct AudioSessionPolicy audio_policy;
static bool platform_initialized;
static id interruption_observer;
static id route_observer;
static id media_reset_observer;
static UIImpactFeedbackGenerator *touch_feedback;

static void report_audio_error(NSString *operation, NSError *error) {
    if (error != nil) {
        fprintf(stderr, "AVAudioSession %s failed: %s\n",
                operation.UTF8String, error.localizedDescription.UTF8String);
    }
}

static void configure_audio_session(bool activate) {
    AVAudioSession *session = [AVAudioSession sharedInstance];
    NSError *error = nil;

    // Ambient is deliberately polite for a handheld port: it respects the
    // silent switch and lets music or podcasts from another app continue.
    [session setCategory:AVAudioSessionCategoryAmbient
                    mode:AVAudioSessionModeDefault
                 options:AVAudioSessionCategoryOptionMixWithOthers
                   error:&error];
    report_audio_error(@"category", error);

    if (activate) {
        error = nil;
        [session setActive:YES error:&error];
        report_audio_error(@"activation", error);
    }
}

static void apply_audio_event(enum AudioSessionEvent event) {
    bool should_play = audio_session_policy_handle(&audio_policy, event);
    AVAudioSession *session = [AVAudioSession sharedInstance];

    audio_sdl_pause(!should_play);
    if (should_play) {
        configure_audio_session(true);
    } else if (event == AUDIO_SESSION_APP_RESIGNED_ACTIVE) {
        NSError *error = nil;
        [session setActive:NO
               withOptions:AVAudioSessionSetActiveOptionNotifyOthersOnDeactivation
                     error:&error];
        report_audio_error(@"deactivation", error);
    }
}

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
    apply_audio_event(active ? AUDIO_SESSION_APP_BECAME_ACTIVE
                             : AUDIO_SESSION_APP_RESIGNED_ACTIVE);
}

void ios_platform_init(void) {
    if (platform_initialized) {
        return;
    }
    platform_initialized = true;

    audio_session_policy_init(&audio_policy, true);
    configure_audio_session(true);
    touch_set_haptic_callback(fire_touch_haptic);

    NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
    interruption_observer = [center
        addObserverForName:AVAudioSessionInterruptionNotification
                    object:nil
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(NSNotification *note) {
        AVAudioSessionInterruptionType type =
            (AVAudioSessionInterruptionType)[note.userInfo[AVAudioSessionInterruptionTypeKey]
                unsignedIntegerValue];
        if (type == AVAudioSessionInterruptionTypeBegan) {
            apply_audio_event(AUDIO_SESSION_INTERRUPTION_BEGAN);
        } else {
            AVAudioSessionInterruptionOptions options =
                (AVAudioSessionInterruptionOptions)[note.userInfo[AVAudioSessionInterruptionOptionKey]
                    unsignedIntegerValue];
            apply_audio_event((options & AVAudioSessionInterruptionOptionShouldResume)
                                  ? AUDIO_SESSION_INTERRUPTION_ENDED_RESUME
                                  : AUDIO_SESSION_INTERRUPTION_ENDED_NO_RESUME);
        }
    }];

    route_observer = [center
        addObserverForName:AVAudioSessionRouteChangeNotification
                    object:nil
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(__unused NSNotification *note) {
        audio_session_policy_handle(&audio_policy, AUDIO_SESSION_ROUTE_CHANGED);
        audio_sdl_route_changed();
    }];

    media_reset_observer = [center
        addObserverForName:AVAudioSessionMediaServicesWereResetNotification
                    object:nil
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(__unused NSNotification *note) {
        configure_audio_session(audio_session_policy_should_play(&audio_policy));
        audio_sdl_route_changed();
    }];
}
