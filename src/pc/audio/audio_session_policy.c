#include "audio_session_policy.h"

void audio_session_policy_init(struct AudioSessionPolicy *policy, bool app_active) {
    policy->app_active = app_active;
    policy->interrupted = false;
    policy->resume_allowed = true;
}

bool audio_session_policy_should_play(const struct AudioSessionPolicy *policy) {
    return policy->app_active && !policy->interrupted && policy->resume_allowed;
}

bool audio_session_policy_handle(struct AudioSessionPolicy *policy,
                                 enum AudioSessionEvent event) {
    switch (event) {
        case AUDIO_SESSION_APP_BECAME_ACTIVE:
            policy->app_active = true;
            if (!policy->interrupted) {
                policy->resume_allowed = true;
            }
            break;
        case AUDIO_SESSION_APP_RESIGNED_ACTIVE:
            policy->app_active = false;
            break;
        case AUDIO_SESSION_INTERRUPTION_BEGAN:
            policy->interrupted = true;
            policy->resume_allowed = false;
            break;
        case AUDIO_SESSION_INTERRUPTION_ENDED_RESUME:
            policy->interrupted = false;
            policy->resume_allowed = true;
            break;
        case AUDIO_SESSION_INTERRUPTION_ENDED_NO_RESUME:
            policy->interrupted = false;
            policy->resume_allowed = false;
            break;
        case AUDIO_SESSION_ROUTE_CHANGED:
            break;
    }
    return audio_session_policy_should_play(policy);
}
