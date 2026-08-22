#if !defined(_WIN32) && !defined(_WIN64)

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#include <SDL2/SDL.h>

#include <ultra64.h>

#include "controller_api.h"
#include "controller_gamepad.h"

static bool init_ok;
static SDL_GameController *sdl_cntrl;

static void controller_sdl_init(void) {
    if (SDL_Init(SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL init error: %s\n", SDL_GetError());
        return;
    }

    init_ok = true;
    controller_gamepad_set_connected(false);
}

static void controller_sdl_read(OSContPad *pad) {
    if (!init_ok) {
        return;
    }

    SDL_GameControllerUpdate();

    if (sdl_cntrl != NULL && !SDL_GameControllerGetAttached(sdl_cntrl)) {
        SDL_GameControllerClose(sdl_cntrl);
        sdl_cntrl = NULL;
        controller_gamepad_set_connected(false);
    }
    if (sdl_cntrl == NULL) {
        for (int i = 0; i < SDL_NumJoysticks(); i++) {
            if (SDL_IsGameController(i)) {
                sdl_cntrl = SDL_GameControllerOpen(i);
                if (sdl_cntrl != NULL) {
                    break;
                }
            }
        }
        if (sdl_cntrl == NULL) {
            controller_gamepad_set_connected(false);
            return;
        }
    }

    controller_gamepad_set_connected(true);
    struct GamepadState state = { 0 };
    static const struct {
        SDL_GameControllerButton button;
        enum GamepadInput input;
    } buttons[] = {
        { SDL_CONTROLLER_BUTTON_A, GAMEPAD_INPUT_SOUTH },
        { SDL_CONTROLLER_BUTTON_B, GAMEPAD_INPUT_EAST },
        { SDL_CONTROLLER_BUTTON_X, GAMEPAD_INPUT_WEST },
        { SDL_CONTROLLER_BUTTON_Y, GAMEPAD_INPUT_NORTH },
        { SDL_CONTROLLER_BUTTON_BACK, GAMEPAD_INPUT_BACK },
        { SDL_CONTROLLER_BUTTON_START, GAMEPAD_INPUT_START },
        { SDL_CONTROLLER_BUTTON_LEFTSTICK, GAMEPAD_INPUT_LEFT_STICK },
        { SDL_CONTROLLER_BUTTON_RIGHTSTICK, GAMEPAD_INPUT_RIGHT_STICK },
        { SDL_CONTROLLER_BUTTON_LEFTSHOULDER, GAMEPAD_INPUT_LEFT_SHOULDER },
        { SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, GAMEPAD_INPUT_RIGHT_SHOULDER },
        { SDL_CONTROLLER_BUTTON_DPAD_UP, GAMEPAD_INPUT_DPAD_UP },
        { SDL_CONTROLLER_BUTTON_DPAD_DOWN, GAMEPAD_INPUT_DPAD_DOWN },
        { SDL_CONTROLLER_BUTTON_DPAD_LEFT, GAMEPAD_INPUT_DPAD_LEFT },
        { SDL_CONTROLLER_BUTTON_DPAD_RIGHT, GAMEPAD_INPUT_DPAD_RIGHT },
    };

    for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
        if (SDL_GameControllerGetButton(sdl_cntrl, buttons[i].button)) {
            state.buttons |= GAMEPAD_BUTTON_BIT(buttons[i].input);
        }
    }

    state.left_x = SDL_GameControllerGetAxis(sdl_cntrl, SDL_CONTROLLER_AXIS_LEFTX);
    state.left_y = SDL_GameControllerGetAxis(sdl_cntrl, SDL_CONTROLLER_AXIS_LEFTY);
    state.right_x = SDL_GameControllerGetAxis(sdl_cntrl, SDL_CONTROLLER_AXIS_RIGHTX);
    state.right_y = SDL_GameControllerGetAxis(sdl_cntrl, SDL_CONTROLLER_AXIS_RIGHTY);
    state.left_trigger = SDL_GameControllerGetAxis(sdl_cntrl, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
    state.right_trigger = SDL_GameControllerGetAxis(sdl_cntrl, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);

#ifdef TARGET_WEB
    // Firefox has a bug: https://bugzilla.mozilla.org/show_bug.cgi?id=1606562
    // It sets down y to 32768.0f / 32767.0f, which is greater than the allowed 1.0f,
    // which SDL then converts to a int16_t by multiplying by 32767.0f, which overflows into -32768.
    // Maximum up will hence never become -32768 with the current version of SDL2,
    // so this workaround should be safe in compliant browsers.
    if (state.left_y == -32768) {
        state.left_y = 32767;
    }
    if (state.right_y == -32768) {
        state.right_y = 32767;
    }
#endif

    controller_gamepad_apply(&state, pad);
}

struct ControllerAPI controller_sdl = {
    controller_sdl_init,
    controller_sdl_read
};

#endif
