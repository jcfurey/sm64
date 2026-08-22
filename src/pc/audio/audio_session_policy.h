#ifndef AUDIO_SESSION_POLICY_H
#define AUDIO_SESSION_POLICY_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum AudioSessionEvent {
    AUDIO_SESSION_APP_BECAME_ACTIVE,
    AUDIO_SESSION_APP_RESIGNED_ACTIVE,
    AUDIO_SESSION_INTERRUPTION_BEGAN,
    AUDIO_SESSION_INTERRUPTION_ENDED_RESUME,
    AUDIO_SESSION_INTERRUPTION_ENDED_NO_RESUME,
    AUDIO_SESSION_ROUTE_CHANGED,
};

struct AudioSessionPolicy {
    bool app_active;
    bool interrupted;
    bool resume_allowed;
};

void audio_session_policy_init(struct AudioSessionPolicy *policy, bool app_active);
bool audio_session_policy_handle(struct AudioSessionPolicy *policy,
                                 enum AudioSessionEvent event);
bool audio_session_policy_should_play(const struct AudioSessionPolicy *policy);

#ifdef __cplusplus
}
#endif

#endif
