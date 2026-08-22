#ifndef CONTROLLER_GAMEPAD_H
#define CONTROLLER_GAMEPAD_H

#include <stdbool.h>
#include <stdint.h>

#include <ultra64.h>

enum GamepadInput {
    GAMEPAD_INPUT_NONE,
    GAMEPAD_INPUT_SOUTH,
    GAMEPAD_INPUT_EAST,
    GAMEPAD_INPUT_WEST,
    GAMEPAD_INPUT_NORTH,
    GAMEPAD_INPUT_BACK,
    GAMEPAD_INPUT_START,
    GAMEPAD_INPUT_LEFT_STICK,
    GAMEPAD_INPUT_RIGHT_STICK,
    GAMEPAD_INPUT_LEFT_SHOULDER,
    GAMEPAD_INPUT_RIGHT_SHOULDER,
    GAMEPAD_INPUT_DPAD_UP,
    GAMEPAD_INPUT_DPAD_DOWN,
    GAMEPAD_INPUT_DPAD_LEFT,
    GAMEPAD_INPUT_DPAD_RIGHT,
    GAMEPAD_INPUT_LEFT_TRIGGER,
    GAMEPAD_INPUT_RIGHT_TRIGGER,
    GAMEPAD_INPUT_RIGHT_STICK_UP,
    GAMEPAD_INPUT_RIGHT_STICK_DOWN,
    GAMEPAD_INPUT_RIGHT_STICK_LEFT,
    GAMEPAD_INPUT_RIGHT_STICK_RIGHT,
    GAMEPAD_INPUT_LEFT_SIDE,
    GAMEPAD_INPUT_RIGHT_SIDE,
    GAMEPAD_INPUT_COUNT,
};

#define GAMEPAD_DEFAULT_A       GAMEPAD_INPUT_SOUTH
#define GAMEPAD_DEFAULT_B       GAMEPAD_INPUT_WEST
#define GAMEPAD_DEFAULT_START   GAMEPAD_INPUT_START
#define GAMEPAD_DEFAULT_Z       GAMEPAD_INPUT_LEFT_SIDE
#define GAMEPAD_DEFAULT_R       GAMEPAD_INPUT_RIGHT_SIDE
#define GAMEPAD_DEFAULT_C_UP    GAMEPAD_INPUT_RIGHT_STICK_UP
#define GAMEPAD_DEFAULT_C_DOWN  GAMEPAD_INPUT_RIGHT_STICK_DOWN
#define GAMEPAD_DEFAULT_C_LEFT  GAMEPAD_INPUT_RIGHT_STICK_LEFT
#define GAMEPAD_DEFAULT_C_RIGHT GAMEPAD_INPUT_RIGHT_STICK_RIGHT

#define GAMEPAD_BUTTON_BIT(input) (1u << (unsigned int)(input))

struct GamepadState {
    uint32_t buttons;
    int16_t left_x;
    int16_t left_y;
    int16_t right_x;
    int16_t right_y;
    int16_t left_trigger;
    int16_t right_trigger;
};

bool controller_gamepad_input_valid(unsigned int input);
const char *controller_gamepad_input_name(unsigned int input);
void controller_gamepad_apply(const struct GamepadState *state, OSContPad *pad);
void controller_gamepad_reset_bindings(void);

void controller_gamepad_set_connected(bool connected);
bool controller_gamepad_is_connected(void);

#endif
