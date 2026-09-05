// configfile.c - handles loading and saving the configuration options
#include <stdbool.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>

#include "configfile.h"
#include "fs.h"
#include "controller/controller_gamepad.h"

#define ARRAY_LEN(arr) (sizeof(arr) / sizeof(arr[0]))
#define MAX_CONFIG_LINE_SIZE (64 * 1024)

enum ConfigOptionType {
    CONFIG_TYPE_BOOL,
    CONFIG_TYPE_UINT,
    CONFIG_TYPE_FLOAT,
};

struct ConfigOption {
    const char *name;
    enum ConfigOptionType type;
    union {
        bool *boolValue;
        unsigned int *uintValue;
        float *floatValue;
    };
};

/*
 *Config options and default values
 */
bool configFullscreen            = false;
unsigned int configFrameCap      = 0;
unsigned int configViewMode      = 0;
bool configRetroMode             = false;
bool configShowFPS               = false;
bool configHUD                   = true;
bool configDebugInfo             = false;
bool configLevelSelect           = false;
bool configTouchHaptics          = true;
bool configTouchAutoHide         = false;
float configTouchScale           = 1.0f;
float configTouchOpacity         = 1.0f;
// Keyboard mappings (scancode values)
unsigned int configKeyA          = 0x26;
unsigned int configKeyB          = 0x33;
unsigned int configKeyStart      = 0x39;
unsigned int configKeyR          = 0x36;
unsigned int configKeyZ          = 0x25;
unsigned int configKeyCUp        = 0x148;
unsigned int configKeyCDown      = 0x150;
unsigned int configKeyCLeft      = 0x14B;
unsigned int configKeyCRight     = 0x14D;
unsigned int configKeyStickUp    = 0x11;
unsigned int configKeyStickDown  = 0x1F;
unsigned int configKeyStickLeft  = 0x1E;
unsigned int configKeyStickRight = 0x20;
unsigned int configGamepadA      = GAMEPAD_DEFAULT_A;
unsigned int configGamepadB      = GAMEPAD_DEFAULT_B;
unsigned int configGamepadStart  = GAMEPAD_DEFAULT_START;
unsigned int configGamepadR      = GAMEPAD_DEFAULT_R;
unsigned int configGamepadZ      = GAMEPAD_DEFAULT_Z;
unsigned int configGamepadCUp    = GAMEPAD_DEFAULT_C_UP;
unsigned int configGamepadCDown  = GAMEPAD_DEFAULT_C_DOWN;
unsigned int configGamepadCLeft  = GAMEPAD_DEFAULT_C_LEFT;
unsigned int configGamepadCRight = GAMEPAD_DEFAULT_C_RIGHT;


static const struct ConfigOption options[] = {
    {.name = "fullscreen",     .type = CONFIG_TYPE_BOOL, .boolValue = &configFullscreen},
    {.name = "frame_cap",      .type = CONFIG_TYPE_UINT, .uintValue = &configFrameCap},
    {.name = "view_mode",      .type = CONFIG_TYPE_UINT, .uintValue = &configViewMode},
    {.name = "retro_mode",     .type = CONFIG_TYPE_BOOL, .boolValue = &configRetroMode},
    {.name = "show_fps",       .type = CONFIG_TYPE_BOOL, .boolValue = &configShowFPS},
    {.name = "hud",            .type = CONFIG_TYPE_BOOL, .boolValue = &configHUD},
    {.name = "debug_info",     .type = CONFIG_TYPE_BOOL, .boolValue = &configDebugInfo},
    {.name = "level_select",   .type = CONFIG_TYPE_BOOL, .boolValue = &configLevelSelect},
    {.name = "touch_haptics", .type = CONFIG_TYPE_BOOL, .boolValue = &configTouchHaptics},
    {.name = "touch_autohide",.type = CONFIG_TYPE_BOOL, .boolValue = &configTouchAutoHide},
    {.name = "touch_scale",    .type = CONFIG_TYPE_FLOAT, .floatValue = &configTouchScale},
    {.name = "touch_opacity",  .type = CONFIG_TYPE_FLOAT, .floatValue = &configTouchOpacity},
    {.name = "key_a",          .type = CONFIG_TYPE_UINT, .uintValue = &configKeyA},
    {.name = "key_b",          .type = CONFIG_TYPE_UINT, .uintValue = &configKeyB},
    {.name = "key_start",      .type = CONFIG_TYPE_UINT, .uintValue = &configKeyStart},
    {.name = "key_r",          .type = CONFIG_TYPE_UINT, .uintValue = &configKeyR},
    {.name = "key_z",          .type = CONFIG_TYPE_UINT, .uintValue = &configKeyZ},
    {.name = "key_cup",        .type = CONFIG_TYPE_UINT, .uintValue = &configKeyCUp},
    {.name = "key_cdown",      .type = CONFIG_TYPE_UINT, .uintValue = &configKeyCDown},
    {.name = "key_cleft",      .type = CONFIG_TYPE_UINT, .uintValue = &configKeyCLeft},
    {.name = "key_cright",     .type = CONFIG_TYPE_UINT, .uintValue = &configKeyCRight},
    {.name = "key_stickup",    .type = CONFIG_TYPE_UINT, .uintValue = &configKeyStickUp},
    {.name = "key_stickdown",  .type = CONFIG_TYPE_UINT, .uintValue = &configKeyStickDown},
    {.name = "key_stickleft",  .type = CONFIG_TYPE_UINT, .uintValue = &configKeyStickLeft},
    {.name = "key_stickright", .type = CONFIG_TYPE_UINT, .uintValue = &configKeyStickRight},
    {.name = "gamepad_a",      .type = CONFIG_TYPE_UINT, .uintValue = &configGamepadA},
    {.name = "gamepad_b",      .type = CONFIG_TYPE_UINT, .uintValue = &configGamepadB},
    {.name = "gamepad_start",  .type = CONFIG_TYPE_UINT, .uintValue = &configGamepadStart},
    {.name = "gamepad_r",      .type = CONFIG_TYPE_UINT, .uintValue = &configGamepadR},
    {.name = "gamepad_z",      .type = CONFIG_TYPE_UINT, .uintValue = &configGamepadZ},
    {.name = "gamepad_cup",    .type = CONFIG_TYPE_UINT, .uintValue = &configGamepadCUp},
    {.name = "gamepad_cdown",  .type = CONFIG_TYPE_UINT, .uintValue = &configGamepadCDown},
    {.name = "gamepad_cleft",  .type = CONFIG_TYPE_UINT, .uintValue = &configGamepadCLeft},
    {.name = "gamepad_cright", .type = CONFIG_TYPE_UINT, .uintValue = &configGamepadCRight},
};

static void validate_gamepad_bindings(void) {
    unsigned int *bindings[] = {
        &configGamepadA, &configGamepadB, &configGamepadStart,
        &configGamepadR, &configGamepadZ, &configGamepadCUp,
        &configGamepadCDown, &configGamepadCLeft, &configGamepadCRight,
    };
    const unsigned int defaults[] = {
        GAMEPAD_DEFAULT_A, GAMEPAD_DEFAULT_B, GAMEPAD_DEFAULT_START,
        GAMEPAD_DEFAULT_R, GAMEPAD_DEFAULT_Z, GAMEPAD_DEFAULT_C_UP,
        GAMEPAD_DEFAULT_C_DOWN, GAMEPAD_DEFAULT_C_LEFT, GAMEPAD_DEFAULT_C_RIGHT,
    };

    for (unsigned int i = 0; i < ARRAY_LEN(bindings); i++) {
        if (*bindings[i] >= GAMEPAD_INPUT_COUNT) {
            *bindings[i] = defaults[i];
        }
    }
}

// Reads an entire line from a file (excluding the newline character) and returns an allocated string
// Returns NULL if no lines could be read from the file
static char *read_file_line(FILE *file) {
    char *buffer;
    size_t bufferSize = 8;
    size_t offset = 0; // offset in buffer to write

    buffer = malloc(bufferSize);
    if (buffer == NULL) {
        fputs("Unable to allocate memory while reading configuration\n", stderr);
        return NULL;
    }
    while (1) {
        // Read a line from the file
        if (fgets(buffer + offset, bufferSize - offset, file) == NULL) {
            // A line can fill the buffer exactly without setting EOF until
            // this next read. Keep the text already read in that case.
            if (offset != 0 && feof(file) && !ferror(file)) {
                break;
            }
            free(buffer);
            return NULL; // Nothing could be read.
        }
        offset = strlen(buffer);
        if (offset == 0) {
            free(buffer);
            return NULL;
        }

        // If a newline was found, remove the trailing newline and exit
        if (buffer[offset - 1] == '\n') {
            buffer[offset - 1] = '\0';
            break;
        }

        if (feof(file)) // EOF was reached
            break;

        // If no newline or EOF was reached, then the whole line wasn't read.
        if (bufferSize >= MAX_CONFIG_LINE_SIZE) {
            int ch = fgetc(file);
            // An exactly full final buffer is still a valid line. Peek at
            // the terminator before treating it as oversized.
            if (ch == '\n' || (ch == EOF && !ferror(file))) {
                break;
            }
            fprintf(stderr, "Ignoring a configuration line longer than %d bytes\n",
                    MAX_CONFIG_LINE_SIZE - 1);
            while (ch != '\n' && ch != EOF) {
                ch = fgetc(file);
            }
            buffer[0] = '\0';
            break;
        }
        size_t newSize = bufferSize * 2;
        char *newBuffer = realloc(buffer, newSize);
        if (newBuffer == NULL) {
            fputs("Unable to grow a configuration line buffer\n", stderr);
            free(buffer);
            return NULL;
        }
        buffer = newBuffer;
        bufferSize = newSize;
    }

    return buffer;
}

// Returns the position of the first non-whitespace character
static char *skip_whitespace(char *str) {
    while (isspace((unsigned char) *str))
        str++;
    return str;
}

// NULL-terminates the current whitespace-delimited word, and returns a pointer to the next word
static char *word_split(char *str) {
    // Precondition: str must not point to whitespace
    assert(!isspace((unsigned char) *str));

    // Find either the next whitespace char or end of string
    while (*str != '\0' && !isspace((unsigned char) *str))
        str++;
    if (*str == '\0') // End of string
        return str;

    // Terminate current word
    *(str++) = '\0';

    // Skip whitespace to next word
    return skip_whitespace(str);
}

static bool parse_uint(const char *text, unsigned int *value) {
    char *end;
    unsigned long parsed;

    if (text[0] == '\0' || text[0] == '-') {
        return false;
    }
    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (errno == ERANGE || *end != '\0' || parsed > UINT_MAX) {
        return false;
    }
    *value = (unsigned int) parsed;
    return true;
}

static bool parse_float(const char *text, float *value) {
    char *end;
    float parsed;

    errno = 0;
    parsed = strtof(text, &end);
    if (text[0] == '\0' || errno == ERANGE || *end != '\0' || !isfinite(parsed)) {
        return false;
    }
    *value = parsed;
    return true;
}

// Splits a string into words, and stores the words into the 'tokens' array
// 'maxTokens' is the length of the 'tokens' array
// Returns the number of tokens parsed
static unsigned int tokenize_string(char *str, int maxTokens, char **tokens) {
    int count = 0;

    str = skip_whitespace(str);
    while (str[0] != '\0' && count < maxTokens) {
        tokens[count] = str;
        str = word_split(str);
        count++;
    }
    return count;
}

// Loads the config file specified by 'filename'
void configfile_load(const char *filename) {
    FILE *file;
    char *line;

    printf("Loading configuration from '%s'\n", filename);

    file = fopen(filename, "r");
    if (file == NULL) {
        // Create a new config file and save defaults
        printf("Config file '%s' not found. Creating it.\n", filename);
        configfile_save(filename);
        return;
    }

    // Go through each line in the file
    while ((line = read_file_line(file)) != NULL) {
        char *p = line;
        char *tokens[2];
        int numTokens;

        while (isspace((unsigned char) *p))
            p++;
        numTokens = tokenize_string(p, 2, tokens);
        if (numTokens != 0) {
            if (numTokens == 2) {
                const struct ConfigOption *option = NULL;

                for (unsigned int i = 0; i < ARRAY_LEN(options); i++) {
                    if (strcmp(tokens[0], options[i].name) == 0) {
                        option = &options[i];
                        break;
                    }
                }
                if (option == NULL)
                    printf("unknown option '%s'\n", tokens[0]);
                else {
                    bool valid = false;
                    switch (option->type) {
                        case CONFIG_TYPE_BOOL:
                            if (strcmp(tokens[1], "true") == 0) {
                                *option->boolValue = true;
                                valid = true;
                            } else if (strcmp(tokens[1], "false") == 0) {
                                *option->boolValue = false;
                                valid = true;
                            }
                            break;
                        case CONFIG_TYPE_UINT:
                            valid = parse_uint(tokens[1], option->uintValue);
                            break;
                        case CONFIG_TYPE_FLOAT:
                            valid = parse_float(tokens[1], option->floatValue);
                            break;
                        default:
                            assert(0); // bad type
                    }
                    if (valid) {
                        printf("option: '%s', value: '%s'\n", tokens[0], tokens[1]);
                    } else {
                        fprintf(stderr, "invalid value for option '%s': '%s'\n",
                                tokens[0], tokens[1]);
                    }
                }
            } else
                puts("error: expected value");
        }
        free(line);
    }

    fclose(file);
    validate_gamepad_bindings();
}

// Writes the config file to 'filename'
bool configfile_save(const char *filename) {
    FILE *file;
    bool write_ok = true;

    printf("Saving configuration to '%s'\n", filename);

    file = fs_open_atomic(filename);
    if (file == NULL) {
        fprintf(stderr, "Unable to open configuration temporary file for '%s'\n", filename);
        return false;
    }

    for (unsigned int i = 0; i < ARRAY_LEN(options); i++) {
        const struct ConfigOption *option = &options[i];

        switch (option->type) {
            case CONFIG_TYPE_BOOL:
                write_ok = write_ok && fprintf(file, "%s %s\n", option->name,
                                                *option->boolValue ? "true" : "false") >= 0;
                break;
            case CONFIG_TYPE_UINT:
                write_ok = write_ok && fprintf(file, "%s %u\n", option->name,
                                                *option->uintValue) >= 0;
                break;
            case CONFIG_TYPE_FLOAT:
                write_ok = write_ok && fprintf(file, "%s %f\n", option->name,
                                                *option->floatValue) >= 0;
                break;
            default:
                assert(0); // unknown type
        }
    }

    if (!write_ok) {
        fprintf(stderr, "Unable to write complete configuration to '%s'\n", filename);
        fs_abort_atomic(file, filename);
        return false;
    }
    if (!fs_close_atomic(file, filename)) {
        fprintf(stderr, "Unable to commit configuration to '%s'\n", filename);
        return false;
    }
    return true;
}
