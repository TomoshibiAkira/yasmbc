#ifndef SMB2_AUDIO_VERIFIER_H
#define SMB2_AUDIO_VERIFIER_H

#include <stdint.h>

/* Frozen named projection in canonical RAM-address order: $f0-$ff followed
 * by the persistent sound-driver bytes selected from $07b0-$07ca. */
typedef struct AudioVerifierState {
    uint8_t note_table_offset;
    uint8_t square1_buffer, square2_buffer, noise_buffer, area_buffer;
    uint8_t music_data_low, music_data_high;
    uint8_t music_offset_square2, music_offset_square1, music_offset_triangle;
    uint8_t pause_queue, area_music_queue, event_music_queue, noise_queue;
    uint8_t square2_queue, square1_queue;
    uint8_t music_offset_noise, event_music_buffer, pause_buffer;
    uint8_t square2_note_length_buffer, square2_note_length_counter;
    uint8_t square2_envelope, square1_note_length_counter, square1_envelope;
    uint8_t triangle_note_length_buffer, triangle_note_length_counter;
    uint8_t noise_beat_length_counter, square1_sfx_length_counter;
    uint8_t square2_sfx_length_counter, sfx_secondary_counter;
    uint8_t noise_sfx_length_counter, dac_counter, noise_loopback_offset;
    uint8_t note_length_table_adder, area_music_buffer_alt, pause_mode_flag;
    uint8_t ground_music_header_offset, alternate_register_content_flag;
} AudioVerifierState;

#endif
