#include <stddef.h>

#include "controller_gamepad.h"
#include "../configfile.h"

#define DEADZONE 4960
#define DIGITAL_AXIS_THRESHOLD 0x4000
#define TRIGGER_THRESHOLD (30 * 256)

struct GamepadBinding {
    unsigned int *input;
    uint16_t mask;
};

static bool gamepad_connected;

static const char *const input_names[GAMEPAD_INPUT_COUNT] = {
    [GAMEPAD_INPUT_NONE] = "NONE",
    [GAMEPAD_INPUT_SOUTH] = "A / CROSS",
    [GAMEPAD_INPUT_EAST] = "B / CIRCLE",
    [GAMEPAD_INPUT_WEST] = "X / SQUARE",
    [GAMEPAD_INPUT_NORTH] = "Y / TRIANGLE",
    [GAMEPAD_INPUT_BACK] = "BACK",
    [GAMEPAD_INPUT_START] = "START",
    [GAMEPAD_INPUT_LEFT_STICK] = "LEFT STICK",
    [GAMEPAD_INPUT_RIGHT_STICK] = "RIGHT STICK",
    [GAMEPAD_INPUT_LEFT_SHOULDER] = "LEFT SHOULDER",
    [GAMEPAD_INPUT_RIGHT_SHOULDER] = "RIGHT SHOULDER",
    [GAMEPAD_INPUT_DPAD_UP] = "DPAD UP",
    [GAMEPAD_INPUT_DPAD_DOWN] = "DPAD DOWN",
    [GAMEPAD_INPUT_DPAD_LEFT] = "DPAD LEFT",
    [GAMEPAD_INPUT_DPAD_RIGHT] = "DPAD RIGHT",
    [GAMEPAD_INPUT_LEFT_TRIGGER] = "LEFT TRIGGER",
    [GAMEPAD_INPUT_RIGHT_TRIGGER] = "RIGHT TRIGGER",
    [GAMEPAD_INPUT_RIGHT_STICK_UP] = "R STICK UP",
    [GAMEPAD_INPUT_RIGHT_STICK_DOWN] = "R STICK DOWN",
    [GAMEPAD_INPUT_RIGHT_STICK_LEFT] = "R STICK LEFT",
    [GAMEPAD_INPUT_RIGHT_STICK_RIGHT] = "R STICK RIGHT",
    [GAMEPAD_INPUT_LEFT_SIDE] = "L1 OR L2",
    [GAMEPAD_INPUT_RIGHT_SIDE] = "R1 OR R2",
};

bool controller_gamepad_input_valid(unsigned int input) {
    return input < GAMEPAD_INPUT_COUNT;
}

const char *controller_gamepad_input_name(unsigned int input) {
    return controller_gamepad_input_valid(input) ? input_names[input] : "INVALID";
}

static bool button_down(const struct GamepadState *state, enum GamepadInput input) {
    return (state->buttons & GAMEPAD_BUTTON_BIT(input)) != 0;
}

static bool input_active(const struct GamepadState *state, unsigned int input) {
    switch (input) {
        case GAMEPAD_INPUT_NONE: return false;
        case GAMEPAD_INPUT_SOUTH:
        case GAMEPAD_INPUT_EAST:
        case GAMEPAD_INPUT_WEST:
        case GAMEPAD_INPUT_NORTH:
        case GAMEPAD_INPUT_BACK:
        case GAMEPAD_INPUT_START:
        case GAMEPAD_INPUT_LEFT_STICK:
        case GAMEPAD_INPUT_RIGHT_STICK:
        case GAMEPAD_INPUT_LEFT_SHOULDER:
        case GAMEPAD_INPUT_RIGHT_SHOULDER:
        case GAMEPAD_INPUT_DPAD_UP:
        case GAMEPAD_INPUT_DPAD_DOWN:
        case GAMEPAD_INPUT_DPAD_LEFT:
        case GAMEPAD_INPUT_DPAD_RIGHT:
            return button_down(state, (enum GamepadInput) input);
        case GAMEPAD_INPUT_LEFT_TRIGGER:
            return state->left_trigger > TRIGGER_THRESHOLD;
        case GAMEPAD_INPUT_RIGHT_TRIGGER:
            return state->right_trigger > TRIGGER_THRESHOLD;
        case GAMEPAD_INPUT_RIGHT_STICK_UP:
            return state->right_y < -DIGITAL_AXIS_THRESHOLD;
        case GAMEPAD_INPUT_RIGHT_STICK_DOWN:
            return state->right_y > DIGITAL_AXIS_THRESHOLD;
        case GAMEPAD_INPUT_RIGHT_STICK_LEFT:
            return state->right_x < -DIGITAL_AXIS_THRESHOLD;
        case GAMEPAD_INPUT_RIGHT_STICK_RIGHT:
            return state->right_x > DIGITAL_AXIS_THRESHOLD;
        case GAMEPAD_INPUT_LEFT_SIDE:
            return button_down(state, GAMEPAD_INPUT_LEFT_SHOULDER)
                || state->left_trigger > TRIGGER_THRESHOLD;
        case GAMEPAD_INPUT_RIGHT_SIDE:
            return button_down(state, GAMEPAD_INPUT_RIGHT_SHOULDER)
                || state->right_trigger > TRIGGER_THRESHOLD;
        default:
            return false;
    }
}

void controller_gamepad_apply(const struct GamepadState *state, OSContPad *pad) {
    const struct GamepadBinding bindings[] = {
        { &configGamepadA, A_BUTTON },
        { &configGamepadB, B_BUTTON },
        { &configGamepadStart, START_BUTTON },
        { &configGamepadZ, Z_TRIG },
        { &configGamepadR, R_TRIG },
        { &configGamepadCUp, U_CBUTTONS },
        { &configGamepadCDown, D_CBUTTONS },
        { &configGamepadCLeft, L_CBUTTONS },
        { &configGamepadCRight, R_CBUTTONS },
    };

    for (size_t i = 0; i < sizeof(bindings) / sizeof(bindings[0]); i++) {
        if (input_active(state, *bindings[i].input)) {
            pad->button |= bindings[i].mask;
        }
    }

    // The physical D-pad always remains an N64 D-pad as well, which keeps
    // menus navigable even while face-button bindings are being edited.
    if (button_down(state, GAMEPAD_INPUT_DPAD_UP)) pad->button |= U_JPAD;
    if (button_down(state, GAMEPAD_INPUT_DPAD_DOWN)) pad->button |= D_JPAD;
    if (button_down(state, GAMEPAD_INPUT_DPAD_LEFT)) pad->button |= L_JPAD;
    if (button_down(state, GAMEPAD_INPUT_DPAD_RIGHT)) pad->button |= R_JPAD;

    {
        int32_t left_x = state->left_x;
        int32_t left_y = state->left_y;
        uint32_t magnitude_sq = (uint32_t)(left_x * left_x) + (uint32_t)(left_y * left_y);
        if (magnitude_sq > (uint32_t)(DEADZONE * DEADZONE)) {
            // Game expects stick coordinates within -80..80.
            pad->stick_x = (s8)(left_x / 409);
            pad->stick_y = (s8)(-left_y / 409);
        }
    }
}

void controller_gamepad_reset_bindings(void) {
    configGamepadA = GAMEPAD_DEFAULT_A;
    configGamepadB = GAMEPAD_DEFAULT_B;
    configGamepadStart = GAMEPAD_DEFAULT_START;
    configGamepadZ = GAMEPAD_DEFAULT_Z;
    configGamepadR = GAMEPAD_DEFAULT_R;
    configGamepadCUp = GAMEPAD_DEFAULT_C_UP;
    configGamepadCDown = GAMEPAD_DEFAULT_C_DOWN;
    configGamepadCLeft = GAMEPAD_DEFAULT_C_LEFT;
    configGamepadCRight = GAMEPAD_DEFAULT_C_RIGHT;
}

void controller_gamepad_set_connected(bool connected) {
    gamepad_connected = connected;
}

bool controller_gamepad_is_connected(void) {
    return gamepad_connected;
}
