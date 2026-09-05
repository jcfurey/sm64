#include <stdio.h>
#include <string.h>

#include "controller/controller_gamepad.h"
#include "configfile.h"

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

static OSContPad map_state(const struct GamepadState *state) {
    OSContPad pad;
    memset(&pad, 0, sizeof(pad));
    controller_gamepad_apply(state, &pad);
    return pad;
}

int main(void) {
    const char *config_path = "controller_map_config.txt";
    struct GamepadState state = { 0 };
    OSContPad pad;

    printf("configurable gamepad mapping\n");
    controller_gamepad_reset_bindings();

    state.buttons = GAMEPAD_BUTTON_BIT(GAMEPAD_INPUT_SOUTH);
    pad = map_state(&state);
    CHECK((pad.button & A_BUTTON) != 0, "the south face button maps to N64 A by default");

    state.buttons = GAMEPAD_BUTTON_BIT(GAMEPAD_INPUT_LEFT_SHOULDER);
    pad = map_state(&state);
    CHECK((pad.button & Z_TRIG) != 0, "the left shoulder maps to Z through the default side binding");
    state.buttons = 0;
    state.left_trigger = 12000;
    pad = map_state(&state);
    CHECK((pad.button & Z_TRIG) != 0, "the left trigger shares the default Z binding");

    memset(&state, 0, sizeof(state));
    state.right_x = 20000;
    pad = map_state(&state);
    CHECK((pad.button & R_CBUTTONS) != 0, "the right stick maps to the C buttons by default");

    memset(&state, 0, sizeof(state));
    state.buttons = GAMEPAD_BUTTON_BIT(GAMEPAD_INPUT_DPAD_UP);
    pad = map_state(&state);
    CHECK((pad.button & U_JPAD) != 0, "the physical D-pad remains available for menus");

    configGamepadA = GAMEPAD_INPUT_EAST;
    state.buttons = GAMEPAD_BUTTON_BIT(GAMEPAD_INPUT_SOUTH);
    pad = map_state(&state);
    CHECK((pad.button & A_BUTTON) == 0, "changing a binding removes the old input");
    state.buttons = GAMEPAD_BUTTON_BIT(GAMEPAD_INPUT_EAST);
    pad = map_state(&state);
    CHECK((pad.button & A_BUTTON) != 0, "changing a binding enables the new input");

    configGamepadA = GAMEPAD_INPUT_COUNT + 50;
    pad = map_state(&state);
    CHECK((pad.button & A_BUTTON) == 0, "an out-of-range runtime binding is ignored safely");

    configGamepadA = GAMEPAD_INPUT_NORTH;
    CHECK(configfile_save(config_path), "configuration saves report a durable commit");
    configGamepadA = GAMEPAD_INPUT_SOUTH;
    configfile_load(config_path);
    CHECK(configGamepadA == GAMEPAD_INPUT_NORTH, "gamepad bindings round-trip through the config file");

    {
        FILE *file = fopen(config_path, "wb");
        fprintf(file, "gamepad_a 9999\n");
        fclose(file);
    }
    configfile_load(config_path);
    CHECK(configGamepadA == GAMEPAD_DEFAULT_A,
          "an out-of-range saved binding falls back to its default");

    {
        FILE *file = fopen(config_path, "wb");
        configFrameCap = 60;
        configTouchScale = 1.25f;
        configHUD = true;
        fprintf(file, "frame_cap 60fps\ntouch_scale nan\nhud falseish\n");
        fclose(file);
    }
    configfile_load(config_path);
    CHECK(configFrameCap == 60, "a numeric option rejects trailing garbage");
    CHECK(configTouchScale == 1.25f, "a floating option rejects non-finite values");
    CHECK(configHUD, "a boolean option rejects unknown spellings");

    // EOF is detected by a second read when a line fills a buffer exactly.
    // Cover the initial allocation and successive growth boundaries, with
    // and without a newline, and an unrelated setting on the preceding line.
    for (size_t length = 7; length <= 65535; length = length * 2 + 1) {
        for (int newline = 0; newline <= 1; newline++) {
            FILE *file = fopen(config_path, "wb");
            fprintf(file, "frame_cap 60\n");
            for (size_t i = 8; i < length; i++) fputc(' ', file);
            fputs("hud true", file);
            if (newline) fputc('\n', file);
            fclose(file);
            configHUD = false;
            configfile_load(config_path);
            CHECK(configHUD, "a final line after another setting is retained");

            file = fopen(config_path, "wb");
            for (size_t i = 7; i < length; i++) fputc(' ', file);
            fputs("key_a 1", file); // exactly 7, 15, 31, ... bytes
            if (newline) fputc('\n', file);
            fclose(file);
            configKeyA = 0;
            configfile_load(config_path);
            CHECK(configKeyA == 1, "a final line filling the buffer is retained");
        }
    }
    {
        FILE *file = fopen(config_path, "wb");
        fputs("fullscreen true", file);
        fclose(file);
        configFullscreen = false;
        configfile_load(config_path);
        CHECK(configFullscreen, "fullscreen true is accepted without a trailing newline");
    }
    {
        FILE *file = fopen(config_path, "wb");
        fputs("hud false", file);
        for (size_t i = 0; i < 65536; i++) fputc(' ', file);
        fputs("\nfullscreen true", file);
        fclose(file);
        configHUD = true;
        configFullscreen = false;
        configfile_load(config_path);
        CHECK(configHUD && configFullscreen, "an oversized line is skipped without losing the next setting");
    }

    controller_gamepad_set_connected(true);
    CHECK(controller_gamepad_is_connected(), "controller presence is shared with the touch overlay");
    controller_gamepad_set_connected(false);
    CHECK(!controller_gamepad_is_connected(), "controller disconnection restores touch visibility");

    remove(config_path);
    remove("controller_map_config.txt.tmp");
    printf(failures == 0 ? "PASS\n" : "FAILED (%d)\n", failures);
    return failures != 0;
}
