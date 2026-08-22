#include <stdio.h>

#include "audio/audio_session_policy.h"

static int failures;

#define CHECK(cond, what)                                                     \
    do {                                                                      \
        if (!(cond)) {                                                        \
            printf("  FAIL: %s\n", what);                                   \
            failures++;                                                       \
        } else {                                                              \
            printf("  ok:   %s\n", what);                                   \
        }                                                                     \
    } while (0)

int main(void) {
    struct AudioSessionPolicy policy;

    printf("iOS audio session policy\n");
    audio_session_policy_init(&policy, true);
    CHECK(audio_session_policy_should_play(&policy), "an active app starts audible");

    CHECK(!audio_session_policy_handle(&policy, AUDIO_SESSION_INTERRUPTION_BEGAN),
          "an interruption pauses audio");
    CHECK(audio_session_policy_handle(&policy, AUDIO_SESSION_INTERRUPTION_ENDED_RESUME),
          "a resumable interruption restarts audio");

    audio_session_policy_handle(&policy, AUDIO_SESSION_INTERRUPTION_BEGAN);
    CHECK(!audio_session_policy_handle(&policy, AUDIO_SESSION_INTERRUPTION_ENDED_NO_RESUME),
          "a non-resumable interruption stays quiet");
    CHECK(audio_session_policy_handle(&policy, AUDIO_SESSION_APP_BECAME_ACTIVE),
          "a later activation allows playback again");

    CHECK(!audio_session_policy_handle(&policy, AUDIO_SESSION_APP_RESIGNED_ACTIVE),
          "backgrounding pauses audio");
    audio_session_policy_handle(&policy, AUDIO_SESSION_INTERRUPTION_BEGAN);
    CHECK(!audio_session_policy_handle(&policy, AUDIO_SESSION_INTERRUPTION_ENDED_RESUME),
          "an interruption cannot resume while backgrounded");
    CHECK(audio_session_policy_handle(&policy, AUDIO_SESSION_APP_BECAME_ACTIVE),
          "foregrounding after the interruption resumes");

    CHECK(audio_session_policy_handle(&policy, AUDIO_SESSION_ROUTE_CHANGED),
          "a route change preserves the current playback decision");

    printf(failures == 0 ? "PASS\n" : "FAILED (%d)\n", failures);
    return failures != 0;
}
