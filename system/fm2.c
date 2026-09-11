#include "fm2.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

static void set_error(char* error, size_t error_size, const char* message) {
    if (error && error_size) snprintf(error, error_size, "%s", message);
}

static void trim_line(char* line) {
    size_t length = strlen(line);
    while (length && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
        line[--length] = '\0';
    }
}

static int parse_int_value(const char* value, int* output) {
    char* end = NULL;
    long parsed;
    errno = 0;
    parsed = strtol(value, &end, 10);
    if (errno || end == value || *end != '\0') return -1;
    *output = (int)parsed;
    return 0;
}

int fm2_open(Fm2Movie* movie, const char* path, char* error, size_t error_size) {
    char line[512];

    memset(movie, 0, sizeof(*movie));
    movie->version = -1;
    movie->port0 = -1;
    movie->port1 = 0;
    movie->file = fopen(path, "rb");
    if (!movie->file) {
        if (error && error_size) {
            snprintf(error, error_size, "cannot open FM2 '%s': %s", path, strerror(errno));
        }
        return -1;
    }

    while (fgets(line, sizeof(line), movie->file)) {
        char* separator;
        char* key;
        char* value;
        int parsed;

        trim_line(line);
        if (line[0] == '|') {
            snprintf(movie->pending_record, sizeof(movie->pending_record), "%s", line);
            movie->has_pending_record = 1;
            break;
        }
        if (!line[0]) continue;

        separator = strchr(line, ' ');
        if (!separator) continue;
        *separator = '\0';
        key = line;
        value = separator + 1;

        if (strcmp(key, "version") == 0) {
            if (parse_int_value(value, &parsed) == 0) movie->version = parsed;
        } else if (strcmp(key, "palFlag") == 0) {
            if (parse_int_value(value, &parsed) == 0) movie->pal = parsed;
        } else if (strcmp(key, "port0") == 0) {
            if (parse_int_value(value, &parsed) == 0) movie->port0 = parsed;
        } else if (strcmp(key, "port1") == 0) {
            if (parse_int_value(value, &parsed) == 0) movie->port1 = parsed;
        } else if (strcmp(key, "fourscore") == 0) {
            if (parse_int_value(value, &parsed) == 0) movie->fourscore = parsed;
        } else if (strcmp(key, "binary") == 0) {
            if (parse_int_value(value, &parsed) == 0) movie->binary = parsed;
        } else if (strcmp(key, "savestate") == 0) {
            movie->has_savestate = 1;
        }
    }

    if (movie->version != 3) {
        set_error(error, error_size, "unsupported FM2 version (expected text FM2 version 3)");
    } else if (movie->binary) {
        set_error(error, error_size, "binary FM2 input logs are not supported");
    } else if (movie->pal) {
        set_error(error, error_size, "PAL FM2 movies are not supported by the NTSC candidate runner");
    } else if (movie->fourscore) {
        set_error(error, error_size, "Four Score FM2 movies are not supported");
    } else if (movie->port0 != 1) {
        set_error(error, error_size, "FM2 port 0 must be a gamepad");
    } else if (movie->port1 != 0 && movie->port1 != 1) {
        set_error(error, error_size, "FM2 port 1 must be empty or a gamepad");
    } else if (movie->has_savestate) {
        set_error(error, error_size, "savestate-based FM2 movies are not supported; use a power-on movie");
    } else if (!movie->has_pending_record) {
        set_error(error, error_size, "FM2 contains no input records");
    } else {
        return 0;
    }

    fm2_close(movie);
    return -1;
}

int fm2_next(Fm2Movie* movie, uint8_t buttons[2], uint32_t* commands,
             char* error, size_t error_size) {
    char line[512];
    char* command_end;
    char* controller_end;
    char* controller;
    unsigned long command_value;
    uint8_t result[2] = {0, 0};
    size_t controller_length;
    int i;

    if (!movie || !movie->file) {
        set_error(error, error_size, "FM2 movie is not open");
        return -1;
    }

    for (;;) {
        if (movie->has_pending_record) {
            snprintf(line, sizeof(line), "%s", movie->pending_record);
            movie->has_pending_record = 0;
        } else if (!fgets(line, sizeof(line), movie->file)) {
            if (ferror(movie->file)) {
                set_error(error, error_size, "error while reading FM2 input log");
                return -1;
            }
            return 0;
        } else {
            trim_line(line);
        }
        if (line[0] == '|') break;
    }

    errno = 0;
    command_value = strtoul(line + 1, &command_end, 10);
    if (errno || command_end == line + 1 || *command_end != '|') {
        set_error(error, error_size, "malformed FM2 command field");
        return -1;
    }

    controller = command_end + 1;
    controller_end = strchr(controller, '|');
    if (!controller_end) {
        set_error(error, error_size, "malformed FM2 controller field");
        return -1;
    }
    controller_length = (size_t)(controller_end - controller);
    if (controller_length != 8) {
        set_error(error, error_size, "FM2 controller 0 must contain eight RLDUTSBA characters");
        return -1;
    }

    /* FM2 columns are R,L,D,U,T,S,B,A; SMB uses the same bits from 0 to 7. */
    for (i = 0; i < 8; i++)
        if (controller[i] != '.' && controller[i] != ' ')
            result[0] |= (uint8_t)(1u << i);
    controller = controller_end + 1;
    controller_end = strchr(controller, '|');
    if (!controller_end ||
        (movie->port1 == 1 && (size_t)(controller_end - controller) != 8) ||
        (movie->port1 == 0 && controller_end != controller)) {
        set_error(error, error_size, "FM2 controller 1 does not match the port1 header");
        return -1;
    }
    if (movie->port1 == 1)
        for (i = 0; i < 8; i++)
            if (controller[i] != '.' && controller[i] != ' ')
                result[1] |= (uint8_t)(1u << i);

    buttons[0] = result[0];
    buttons[1] = result[1];
    *commands = (uint32_t)command_value;
    movie->frame++;
    return 1;
}

int fm2_skip(Fm2Movie* movie, uint32_t count, char* error, size_t error_size) {
    uint32_t i;
    for (i = 0; i < count; i++) {
        uint8_t buttons[2];
        uint32_t commands;
        int status = fm2_next(movie, buttons, &commands, error, error_size);
        if (status == 0) {
            set_error(error, error_size, "FM2 ended before --tas-start");
            return -1;
        }
        if (status < 0) return -1;
    }
    return 0;
}

void fm2_close(Fm2Movie* movie) {
    if (movie && movie->file) fclose(movie->file);
    if (movie) movie->file = NULL;
}
