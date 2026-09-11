#ifndef SMB_VERIFIER_FM2_H
#define SMB_VERIFIER_FM2_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    FILE* file;
    uint32_t frame;
    int version;
    int pal;
    int port0;
    int port1;
    int fourscore;
    int binary;
    int has_savestate;
    int has_pending_record;
    char pending_record[512];
} Fm2Movie;

int fm2_open(Fm2Movie* movie, const char* path, char* error, size_t error_size);
int fm2_next(Fm2Movie* movie, uint8_t buttons[2], uint32_t* commands,
             char* error, size_t error_size);
int fm2_skip(Fm2Movie* movie, uint32_t count, char* error, size_t error_size);
void fm2_close(Fm2Movie* movie);

#endif
