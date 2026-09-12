/*
 * ASM-first SMB sound driver.
 *
 * The game-specific driver is kept separate from the host 2A03 renderer so
 * Host backends receive the same ordered register writes through the platform
 * API. This file intentionally retains the original RAM/state-machine
 * boundaries, but uses descriptive C names at those boundaries rather than
 * exposing 6502 temporary-register names to the rest of the port.
 */
#include <string.h>

#include "audio.h"
#include "assets.h"

typedef char AudioVerifierState_must_be_38_bytes[sizeof(AudioVerifierState) == 38 ? 1 : -1];
#include "audio-data.h"
#include "constants/defs.h"
#include "constants/globals.h"
#include "system/platform.h"

/* CPU addresses of the 2A03 registers used by SMB.  The values are bus
 * addresses, not host audio-library offsets. */
enum {
    APU_SQUARE1 = 0x4000,
    APU_SQUARE2 = 0x4004,
    APU_TRIANGLE = 0x4008,
    APU_NOISE = 0x400c,
    APU_DMC = 0x4010,
    APU_STATUS = 0x4015,
    APU_FRAME_COUNTER = 0x4017
};

enum {
    APU_STATUS_DISABLE_SQUARE1 = 0x0e,
    APU_STATUS_DISABLE_SQUARE2 = 0x0d,
    APU_STATUS_DISABLE_TRIANGLE = 0x0b,
    APU_STATUS_ENABLE_CHANNELS = 0x0f,
    APU_FRAME_COUNTER_RELOAD = 0xff
};

/* Register offsets are named so writes such as base + 2 read as
 * "timer low" instead of another unexplained literal. */
enum {
    APU_CONTROL_OFFSET = 0,
    APU_SWEEP_OFFSET = 1,
    APU_TIMER_LOW_OFFSET = 2,
    APU_TIMER_HIGH_OFFSET = 3,
    APU_DMC_OUTPUT_OFFSET = 1,
    APU_TIMER_HIGH_LENGTH_FLAG = 0x08
};

/* Addresses inside the extracted audio/music PRG-ROM region.  These names
 * correspond to labels in smb1-disasm/audio/music.asm and main.asm. */
enum {
    MUSIC_HEADER_OFFSET_TABLE = 0xf90c,
    MUSIC_FREQUENCY_TABLE_HIGH = 0xff00,
    MUSIC_FREQUENCY_TABLE_LOW = 0xff01,
    MUSIC_LENGTH_TABLE = 0xff66,
    MUSIC_END_CASTLE_ENVELOPE = 0xff96,
    MUSIC_AREA_ENVELOPE = 0xff9a,
    MUSIC_WATER_EVENT_ENVELOPE = 0xffa2,
    MUSIC_BOWSER_FLAME_ENVELOPE = 0xffc9,
    MUSIC_BRICK_SHATTER_ENVELOPE = 0xffea
};

/* Bit fields encoded in the music stream and the music-selection buffers. */
enum {
    MUSIC_LENGTH_FLAG = 0x80,
    MUSIC_NOTE_MASK = 0x3e,
    MUSIC_LENGTH_INDEX_MASK = 0x07,
    MUSIC_AREA_CONTROL_MASK = 0x7d,
    MUSIC_ENVELOPE_SKIP_MASK = 0x91,
    MUSIC_AREA_LOOP_MASK = 0x5f,
    MUSIC_TRIANGLE_EVENT_MASK = 0x6e,
    MUSIC_TRIANGLE_AREA_MASK = 0x0a,
    MUSIC_NOISE_AREA_MASK = 0xf3,
    MUSIC_DAC_AREA_MASK = 0x03,
    MUSIC_NOISE_LONG_BEAT = 0x30,
    MUSIC_NOISE_STRONG_BEAT = 0x20,
    MUSIC_NOISE_SHORT_BEAT = 0x10,
    MUSIC_CONTROL_END_CASTLE = 0x04,
    MUSIC_CONTROL_WATER_EVENT = 0x28,
    MUSIC_CONTROL_STANDARD = 0x08
};

/* Frame/NMI counters copied from the sound routines. */
enum {
    SFX_JUMP_LENGTH = 0x28,
    SFX_JUMP_SECOND_PHASE = 0x25,
    SFX_JUMP_THIRD_PHASE = 0x20,
    SFX_BUMP_LENGTH = 0x0a,
    SFX_FIREBALL_LENGTH = 0x05,
    SFX_SECOND_TONE_PHASE = 0x06,
    SFX_STOMP_TIMER_PHASE = 0x06,
    SFX_SMACK_SECOND_PHASE = 0x08,
    SFX_SWIM_STOMP_LENGTH = 0x0e,
    SFX_SMACK_LENGTH = 0x0e,
    SFX_PIPE_INJURY_LENGTH = 0x2f,
    SFX_FLAGPOLE_LENGTH = 0x40,
    SFX_BOWSER_FALL_LENGTH = 0x38,
    SFX_BOWSER_FALL_SECOND_PHASE = 0x08,
    SFX_COIN_LENGTH = 0x35,
    SFX_TIMER_LENGTH = 0x06,
    SFX_COIN_TIMER_SECOND_PHASE = 0x30,
    SFX_GROW_POWERUP_LENGTH = 0x10,
    SFX_GROW_VINE_LENGTH = 0x20,
    SFX_BLAST_LENGTH = 0x20,
    SFX_BLAST_SECOND_PHASE = 0x18,
    SFX_POWERUP_GRAB_LENGTH = 0x36,
    SFX_EXTRA_LIFE_LENGTH = 0x30,
    SFX_BRICK_SHATTER_LENGTH = 0x20,
    SFX_BOWSER_FLAME_LENGTH = 0x40,
    PAUSE_SOUND_LENGTH = 0x2a,
    MUSIC_GROUND_HEADER_START = 0x10,
    MUSIC_GROUND_HEADER_END = 0x32,
    MUSIC_GROUND_HEADER_LOOP = 0x11,
    MUSIC_AREA_HEADER_INDEX_START = 0x08,
    MUSIC_LONG_TRIANGLE_NOTE = 0x12,
    DAC_COUNTER_LIMIT = 0x30,
    PAUSE_SECOND_TONE_LATE = 0x24,
    PAUSE_FIRST_TONE_REPEAT = 0x1e,
    PAUSE_SECOND_TONE_EARLY = 0x18,
    PAUSE_FIRST_TONE_NOTE_INDEX = 0x44,
    PAUSE_SECOND_TONE_NOTE_INDEX = 0x64
};

/* Values written to the APU registers.  These are 2A03 duty/envelope,
 * sweep, timer, and length-control bytes—not host audio frequencies.  Giving
 * each group a name keeps the ASM-derived values visible without making the
 * call sites look like an encoded byte stream. */
enum {
    APU_SQUARE_SWEEP_DEFAULT = 0x7f,
    APU_SQUARE_VOLUME_MUSIC = 0x82,
    APU_SQUARE_VOLUME_SILENT = 0x90,
    APU_SQUARE_VOLUME_MUSIC_REST = 0x04,
    APU_SQUARE1_VOLUME_STREAM_END = 0x83,
    APU_SQUARE1_SWEEP_STREAM_END = 0x94,
    APU_TRIANGLE_CONTROL_SHORT = 0x1f,
    APU_TRIANGLE_CONTROL_LONG = 0xff,
    APU_TRIANGLE_CONTROL_CASTLE = 0x0f,
    APU_NOISE_VOLUME_MUSIC = 0x1c,
    APU_NOISE_VOLUME_GENERIC = 0x10,
    APU_NOISE_BOWSER_PERIOD = 0x0f,
    APU_NOISE_LENGTH_LONG = 0x58,
    APU_NOISE_LENGTH_SHORT = 0x18,
    APU_NOISE_VOLUME_SILENT = 0xf0,
    APU_PAUSE_VOLUME = 0x84
};

enum {
    SFX_JUMP_VOLUME = 0x82,
    SFX_JUMP_SWEEP = 0xa7,
    SFX_SMALL_JUMP_NOTE_INDEX = 0x26,
    SFX_BIG_JUMP_NOTE_INDEX = 0x18,
    SFX_JUMP_SECOND_SWEEP = 0xf6,
    SFX_JUMP_SECOND_VOLUME = 0x5f,
    SFX_JUMP_THIRD_SWEEP = 0xbc,
    SFX_JUMP_THIRD_VOLUME = 0x48,
    SFX_BUMP_VOLUME = 0x9e,
    SFX_BUMP_SWEEP = 0x93,
    SFX_FIREBALL_SWEEP = 0x99,
    SFX_BUMP_NOTE_INDEX = 0x0c,
    SFX_BUMP_SECOND_SWEEP = 0xbb,
    SFX_STOMP_SWEEP = 0x9c,
    SFX_STOMP_NOTE_INDEX = 0x26,
    SFX_STOMP_TIMER_LOW = 0x9e,
    SFX_SMACK_VOLUME = 0x9f,
    SFX_SMACK_SWEEP = 0xcb,
    SFX_SMACK_NOTE_INDEX = 0x28,
    SFX_SMACK_TIMER_LOW = 0xa0,
    SFX_SMACK_SECOND_VOLUME = 0x9f,
    SFX_PIPE_VOLUME = 0x9a,
    SFX_PIPE_SWEEP = 0x91,
    SFX_PIPE_NOTE_INDEX = 0x44,
    SFX_FLAGPOLE_NOTE_INDEX = 0x62,
    SFX_FLAGPOLE_SWEEP = 0xbc,
    SFX_FLAGPOLE_VOLUME = 0x99
};

enum {
    SFX_BOWSER_FALL_VOLUME = 0x9f,
    SFX_BOWSER_FALL_SWEEP = 0xc4,
    SFX_BOWSER_FALL_NOTE_INDEX = 0x18,
    SFX_BOWSER_FALL_SECOND_SWEEP = 0xa4,
    SFX_BOWSER_FALL_SECOND_NOTE_INDEX = 0x5a,
    SFX_COIN_VOLUME = 0x8d,
    SFX_TIMER_VOLUME = 0x98,
    SFX_COIN_TIMER_NOTE_INDEX = 0x42,
    SFX_TIMER_SECOND_TIMER_LOW = 0x54,
    SFX_GROW_VOLUME = 0x9d,
    SFX_BLAST_VOLUME = 0x9f,
    SFX_BLAST_SWEEP = 0x94,
    SFX_BLAST_NOTE_INDEX = 0x5e,
    SFX_BLAST_SECOND_SWEEP = 0x93,
    SFX_BLAST_SECOND_NOTE_INDEX = 0x18,
    SFX_POWERUP_GRAB_VOLUME = 0x5d,
    SFX_EXTRA_LIFE_VOLUME = 0x82
};

enum {
    MUSIC_NOISE_LONG_PERIOD = 3,
    MUSIC_NOISE_STRONG_PERIOD = 0x0c,
    MUSIC_NOISE_SHORT_PERIOD = 3,
    MUSIC_SQUARE_INVALID_VOLUME = 0
};

typedef struct AudioState {
    uint8_t square1_buffer;
    uint8_t square2_buffer;
    uint8_t noise_buffer;
    uint8_t area_music_buffer;
    uint8_t pause_buffer;

    uint8_t music_data_low;
    uint8_t music_data_high;
    uint8_t music_offset_square2;
    uint8_t music_offset_square1;
    uint8_t music_offset_triangle;
    uint8_t music_offset_noise;

    uint8_t note_length_table_offset;
    uint8_t square2_note_length_buffer;
    uint8_t square2_note_length_counter;
    uint8_t square2_envelope;
    uint8_t square1_note_length_counter;
    uint8_t square1_envelope;
    uint8_t triangle_note_length_buffer;
    uint8_t triangle_note_length_counter;
    uint8_t noise_beat_length_counter;

    uint8_t square1_sfx_length_counter;
    uint8_t square2_sfx_length_counter;
    uint8_t sfx_secondary_counter;
    uint8_t noise_sfx_length_counter;

    uint8_t dac_counter;
    uint8_t noise_loopback_offset;
    uint8_t note_length_table_adder;
    uint8_t area_music_buffer_alt;
    uint8_t pause_mode_flag;
    uint8_t ground_music_header_offset;
    uint8_t alternate_register_content_flag;
} AudioState;

static AudioState audio_state;

/* InitializeMemory (main.asm:1536-1555) is called with Y=$4b by
 * InitializeArea.  Its page loop clears the sound symbols at $00f0-$00ff,
 * but it does not reach SoundMemory at $07b0.  Keep the high sound bank
 * untouched here, just as the 6502 loop does. */
void Audio_ResetLowMemory(void) {
    audio_state.note_length_table_offset = 0; /* NoteLenLookupTblOfs=$00f0 */
    audio_state.square1_buffer = 0;           /* Square1SoundBuffer=$00f1 */
    audio_state.square2_buffer = 0;           /* Square2SoundBuffer=$00f2 */
    audio_state.noise_buffer = 0;             /* NoiseSoundBuffer=$00f3 */
    audio_state.area_music_buffer = 0;        /* AreaMusicBuffer=$00f4 */
    audio_state.music_data_low = 0;           /* MusicDataLow=$00f5 */
    audio_state.music_data_high = 0;          /* MusicDataHigh=$00f6 */
    audio_state.music_offset_square2 = 0;     /* MusicOffset_Square2=$00f7 */
    audio_state.music_offset_square1 = 0;     /* MusicOffset_Square1=$00f8 */
    audio_state.music_offset_triangle = 0;    /* MusicOffset_Triangle=$00f9 */

    g_PauseSoundQueue = 0;   /* PauseSoundQueue=$00fa */
    g_AreaMusicQueue = 0;    /* AreaMusicQueue=$00fb */
    g_EventMusicQueue = 0;   /* EventMusicQueue=$00fc */
    g_NoiseSoundQueue = 0;   /* NoiseSoundQueue=$00fd */
    g_Square2SoundQueue = 0; /* Square2SoundQueue=$00fe */
    g_Square1SoundQueue = 0; /* Square1SoundQueue=$00ff */
}

/* InitializeGame performs the same low-page clear and then ClrSndLoop
 * (main.asm:1405-1412) over SoundMemory=$07b0-$07cf.  The full struct is
 * the C owner of those physical sound bytes; the queue globals are the
 * corresponding $00f0-$00ff aliases cleared by InitializeMemory. */
void Audio_ResetGameMemory(void) {
    memset(&audio_state, 0, sizeof(audio_state));
    g_EventMusicBuffer = 0; /* EventMusicBuffer=$07b1, outside AudioState. */
    g_PauseSoundQueue = 0;
    g_AreaMusicQueue = 0;
    g_EventMusicQueue = 0;
    g_NoiseSoundQueue = 0;
    g_Square2SoundQueue = 0;
    g_Square1SoundQueue = 0;
}

static uint8_t swim_stomp_envelope[14];
static uint8_t extra_life_frequency[6];
static uint8_t powerup_grab_frequency[27];
static uint8_t powerup_vgrow_frequency[32];
static uint8_t brick_shatter_frequency[16];

int Audio_LoadRomTables(void) {
    if (Assets_Copy("tables/swim_stomp_envelope.bin", swim_stomp_envelope,
                    sizeof(swim_stomp_envelope)) ||
        Assets_Copy("tables/extra_life_freq.bin", extra_life_frequency,
                    sizeof(extra_life_frequency)) ||
        Assets_Copy("tables/powerup_grab_freq.bin", powerup_grab_frequency,
                    sizeof(powerup_grab_frequency)) ||
        Assets_Copy("tables/powerup_vgrow_freq.bin", powerup_vgrow_frequency,
                    sizeof(powerup_vgrow_frequency)) ||
        Assets_Copy("tables/brick_shatter_freq.bin", brick_shatter_frequency,
                    sizeof(brick_shatter_frequency)))
        return -1;
    return 0;
}

static uint8_t read_music_rom(uint16_t cpu_address) {
    unsigned int offset;
    if (cpu_address < SMB_MUSIC_ROM_BASE)
        return 0;
    offset = (unsigned int)(cpu_address - SMB_MUSIC_ROM_BASE);
    return offset < smb_music_rom_len ? smb_music_rom[offset] : 0;
}
static uint16_t music_data_pointer(void) {
    return (uint16_t)((uint16_t)audio_state.music_data_high << 8) | audio_state.music_data_low;
}
static void apu_write(uint16_t address, uint8_t value) { platform_apu_write(address, value); }

/* SoundEngine toggles the channel-enable bits around a length-counter reset.
 * The two writes must remain adjacent and in this order. */
static void reload_apu_status(uint8_t disabled_channels, uint8_t enabled_channels) {
    apu_write(APU_STATUS, disabled_channels);
    apu_write(APU_STATUS, enabled_channels);
}

/* The sound tables store the 2A03 timer divider as two bytes.  The argument
 * is the original note/table index, not a frequency in Hertz. */
static uint8_t set_frequency(uint16_t channel_base, uint8_t note_index) {
    uint8_t timer_low = read_music_rom((uint16_t)(MUSIC_FREQUENCY_TABLE_LOW + note_index));
    if (!timer_low)
        return 0;
    apu_write((uint16_t)(channel_base + APU_TIMER_LOW_OFFSET), timer_low);
    apu_write(
        (uint16_t)(channel_base + APU_TIMER_HIGH_OFFSET),
        (uint8_t)(read_music_rom((uint16_t)(MUSIC_FREQUENCY_TABLE_HIGH + note_index)) |
                  APU_TIMER_HIGH_LENGTH_FLAG));
    return timer_low;
}

/* Square 1 deliberately writes sweep before volume; this is the order in
 * Dump_Squ1_Regs/PlaySqu1Sfx. */
static void play_square1(uint8_t volume, uint8_t sweep, uint8_t note_index) {
    apu_write(APU_SQUARE1 + APU_SWEEP_OFFSET, sweep);
    apu_write(APU_SQUARE1 + APU_CONTROL_OFFSET, volume);
    set_frequency(APU_SQUARE1, note_index);
}

/* Square 2 uses the opposite control-register order in Dump_Sq2_Regs. */
static void play_square2(uint8_t volume, uint8_t sweep, uint8_t note_index) {
    apu_write(APU_SQUARE2 + APU_CONTROL_OFFSET, volume);
    apu_write(APU_SQUARE2 + APU_SWEEP_OFFSET, sweep);
    set_frequency(APU_SQUARE2, note_index);
}
static void stop_square1(void) {
    audio_state.square1_buffer = 0;
    reload_apu_status(APU_STATUS_DISABLE_SQUARE1, APU_STATUS_ENABLE_CHANNELS);
}
static void stop_square2(void) {
    audio_state.square2_buffer = 0;
    reload_apu_status(APU_STATUS_DISABLE_SQUARE2, APU_STATUS_ENABLE_CHANNELS);
}
static void silence_square2(void) {
    reload_apu_status(APU_STATUS_DISABLE_SQUARE2, APU_STATUS_ENABLE_CHANNELS);
}

/* The queue is a priority bitfield.  Bit 7 is tested first by the ASM; for
 * the remaining bits, the two's-complement low-bit operation reproduces the
 * successive LSR/BCS tests without changing the queue value. */
static uint8_t select_sound_effect(uint8_t queue_bits) {
    if (queue_bits & SFX_SMALL_JUMP)
        return SFX_SMALL_JUMP;
    return (uint8_t)(queue_bits & (uint8_t)(0u - queue_bits));
}

static void handle_square1_jump(uint8_t effect, int is_start) {
    if (is_start) {
        play_square1(SFX_JUMP_VOLUME, SFX_JUMP_SWEEP,
                     effect == Sfx_SmallJump ? SFX_SMALL_JUMP_NOTE_INDEX : SFX_BIG_JUMP_NOTE_INDEX);
        audio_state.square1_sfx_length_counter = SFX_JUMP_LENGTH;
        return;
    }

    if (audio_state.square1_sfx_length_counter == SFX_JUMP_SECOND_PHASE) {
        apu_write(APU_SQUARE1 + APU_SWEEP_OFFSET, SFX_JUMP_SECOND_SWEEP);
        apu_write(APU_SQUARE1 + APU_CONTROL_OFFSET, SFX_JUMP_SECOND_VOLUME);
    } else if (audio_state.square1_sfx_length_counter == SFX_JUMP_THIRD_PHASE) {
        apu_write(APU_SQUARE1 + APU_SWEEP_OFFSET, SFX_JUMP_THIRD_SWEEP);
        apu_write(APU_SQUARE1 + APU_CONTROL_OFFSET, SFX_JUMP_THIRD_VOLUME);
    }
}

static void handle_square1_bump_or_fireball(uint8_t effect, int is_start) {
    if (is_start) {
        play_square1(SFX_BUMP_VOLUME, effect == Sfx_Bump ? SFX_BUMP_SWEEP : SFX_FIREBALL_SWEEP,
                     SFX_BUMP_NOTE_INDEX);
        audio_state.square1_sfx_length_counter =
            effect == Sfx_Bump ? SFX_BUMP_LENGTH : SFX_FIREBALL_LENGTH;
    } else if (audio_state.square1_sfx_length_counter == SFX_SECOND_TONE_PHASE) {
        apu_write(APU_SQUARE1 + APU_SWEEP_OFFSET, SFX_BUMP_SECOND_SWEEP);
    }
}

static void handle_square1_stomp(int is_start) {
    if (is_start) {
        play_square1(SFX_BUMP_VOLUME, SFX_STOMP_SWEEP, SFX_STOMP_NOTE_INDEX);
        audio_state.square1_sfx_length_counter = SFX_SWIM_STOMP_LENGTH;
    }

    {
        uint8_t counter = audio_state.square1_sfx_length_counter;
        if (counter && counter <= sizeof(swim_stomp_envelope))
            apu_write(APU_SQUARE1 + APU_CONTROL_OFFSET, swim_stomp_envelope[counter - 1]);
        if (counter == SFX_STOMP_TIMER_PHASE)
            apu_write(APU_SQUARE1 + APU_TIMER_LOW_OFFSET, SFX_STOMP_TIMER_LOW);
    }
}

static void handle_square1_smacked_enemy(int is_start) {
    if (is_start) {
        play_square1(SFX_SMACK_VOLUME, SFX_SMACK_SWEEP, SFX_SMACK_NOTE_INDEX);
        audio_state.square1_sfx_length_counter = SFX_SMACK_LENGTH;
        return;
    }

    if (audio_state.square1_sfx_length_counter == SFX_SMACK_SECOND_PHASE)
        apu_write(APU_SQUARE1 + APU_TIMER_LOW_OFFSET, SFX_SMACK_TIMER_LOW);
    apu_write(APU_SQUARE1 + APU_CONTROL_OFFSET,
              audio_state.square1_sfx_length_counter == SFX_SMACK_SECOND_PHASE
                  ? SFX_SMACK_SECOND_VOLUME
                  : APU_SQUARE_VOLUME_SILENT);
}

static void handle_square1_pipe_injury(int is_start) {
    if (is_start)
        audio_state.square1_sfx_length_counter = SFX_PIPE_INJURY_LENGTH;

    if ((((audio_state.square1_sfx_length_counter >> 2) & 2) != 0) &&
        !(audio_state.square1_sfx_length_counter & 3))
        play_square1(SFX_PIPE_VOLUME, SFX_PIPE_SWEEP, SFX_PIPE_NOTE_INDEX);
}

static void handle_square1_flagpole(int is_start) {
    if (!is_start)
        return;

    audio_state.square1_sfx_length_counter = SFX_FLAGPOLE_LENGTH;
    set_frequency(APU_SQUARE1, SFX_FLAGPOLE_NOTE_INDEX);
    apu_write(APU_SQUARE1 + APU_SWEEP_OFFSET, SFX_FLAGPOLE_SWEEP);
    apu_write(APU_SQUARE1 + APU_CONTROL_OFFSET, SFX_FLAGPOLE_VOLUME);
}

static void handle_square1_sfx(void) {
    uint8_t queue_bits = g_Square1SoundQueue ? g_Square1SoundQueue : audio_state.square1_buffer;
    uint8_t selected_effect;
    int is_start = g_Square1SoundQueue != 0;

    if (!queue_bits)
        return;
    if (is_start)
        audio_state.square1_buffer = queue_bits;

    selected_effect = select_sound_effect(queue_bits);
    switch (selected_effect) {
    case Sfx_SmallJump:
    case Sfx_BigJump:
        handle_square1_jump(selected_effect, is_start);
        break;
    case Sfx_Bump:
    case Sfx_Fireball:
        handle_square1_bump_or_fireball(selected_effect, is_start);
        break;
    case Sfx_EnemyStomp:
        handle_square1_stomp(is_start);
        break;
    case Sfx_EnemySmack:
        handle_square1_smacked_enemy(is_start);
        break;
    case Sfx_PipeDown_Injury:
        handle_square1_pipe_injury(is_start);
        break;
    case Sfx_Flagpole:
        handle_square1_flagpole(is_start);
        break;
    default:
        break;
    }

    if (audio_state.square1_sfx_length_counter && --audio_state.square1_sfx_length_counter == 0)
        stop_square1();
}

static void handle_square2_bowser_fall(int is_start) {
    if (is_start) {
        audio_state.square2_sfx_length_counter = SFX_BOWSER_FALL_LENGTH;
        play_square2(SFX_BOWSER_FALL_VOLUME, SFX_BOWSER_FALL_SWEEP, SFX_BOWSER_FALL_NOTE_INDEX);
    } else if (audio_state.square2_sfx_length_counter == SFX_BOWSER_FALL_SECOND_PHASE) {
        play_square2(SFX_BOWSER_FALL_VOLUME, SFX_BOWSER_FALL_SECOND_SWEEP,
                     SFX_BOWSER_FALL_SECOND_NOTE_INDEX);
    }
}

static void handle_square2_coin_or_timer(uint8_t effect, int is_start) {
    if (is_start) {
        audio_state.square2_sfx_length_counter =
            effect == Sfx_CoinGrab ? SFX_COIN_LENGTH : SFX_TIMER_LENGTH;
        play_square2(effect == Sfx_CoinGrab ? SFX_COIN_VOLUME : SFX_TIMER_VOLUME,
                     APU_SQUARE_SWEEP_DEFAULT, SFX_COIN_TIMER_NOTE_INDEX);
    } else if (audio_state.square2_sfx_length_counter == SFX_COIN_TIMER_SECOND_PHASE) {
        apu_write(APU_SQUARE2 + APU_TIMER_LOW_OFFSET, SFX_TIMER_SECOND_TIMER_LOW);
    }
}

/* Growth sounds use a secondary counter.  The normal SFX length counter is
 * intentionally not decremented here; this is the ContinueGrowItems path. */
static void handle_square2_growth(uint8_t effect, int is_start) {
    if (is_start) {
        audio_state.square2_sfx_length_counter =
            effect == Sfx_GrowPowerUp ? SFX_GROW_POWERUP_LENGTH : SFX_GROW_VINE_LENGTH;
        audio_state.sfx_secondary_counter = 0;
        apu_write(APU_SQUARE2 + APU_SWEEP_OFFSET, APU_SQUARE_SWEEP_DEFAULT);
    }

    ++audio_state.sfx_secondary_counter;
    {
        uint8_t table_index = (uint8_t)(audio_state.sfx_secondary_counter >> 1);
        if (table_index == audio_state.square2_sfx_length_counter) {
            stop_square2();
            return;
        }
        apu_write(APU_SQUARE2 + APU_CONTROL_OFFSET, SFX_GROW_VOLUME);
        set_frequency(APU_SQUARE2, powerup_vgrow_frequency[table_index]);
    }
}

static void handle_square2_blast(int is_start) {
    if (is_start) {
        audio_state.square2_sfx_length_counter = SFX_BLAST_LENGTH;
        play_square2(SFX_BLAST_VOLUME, SFX_BLAST_SWEEP, SFX_BLAST_NOTE_INDEX);
    } else if (audio_state.square2_sfx_length_counter == SFX_BLAST_SECOND_PHASE) {
        play_square2(SFX_BLAST_VOLUME, SFX_BLAST_SECOND_SWEEP, SFX_BLAST_SECOND_NOTE_INDEX);
    }
}

static void handle_square2_powerup_grab(int is_start) {
    if (is_start)
        audio_state.square2_sfx_length_counter = SFX_POWERUP_GRAB_LENGTH;
    if (!(audio_state.square2_sfx_length_counter & 1)) {
        uint8_t table_index = (uint8_t)(audio_state.square2_sfx_length_counter >> 1);
        if (table_index && table_index <= sizeof(powerup_grab_frequency))
            play_square2(SFX_POWERUP_GRAB_VOLUME, APU_SQUARE_SWEEP_DEFAULT,
                         powerup_grab_frequency[table_index - 1]);
    }
}

static void handle_square2_extra_life(int is_start) {
    if (is_start)
        audio_state.square2_sfx_length_counter = SFX_EXTRA_LIFE_LENGTH;
    if (!(audio_state.square2_sfx_length_counter & 7)) {
        uint8_t table_index = (uint8_t)(audio_state.square2_sfx_length_counter >> 3);
        if (table_index && table_index <= sizeof(extra_life_frequency))
            play_square2(SFX_EXTRA_LIFE_VOLUME, APU_SQUARE_SWEEP_DEFAULT,
                         extra_life_frequency[table_index - 1]);
    }
}

static void handle_square2_sfx(void) {
    uint8_t queue_bits;
    uint8_t selected_effect;
    int is_start;

    /* Extra life has the same channel-locking special case as the ASM. */
    if (audio_state.square2_buffer & Sfx_ExtraLife) {
        queue_bits = audio_state.square2_buffer;
        is_start = 0;
    } else {
        queue_bits = g_Square2SoundQueue ? g_Square2SoundQueue : audio_state.square2_buffer;
        is_start = g_Square2SoundQueue != 0;
    }
    if (!queue_bits)
        return;
    if (is_start)
        audio_state.square2_buffer = queue_bits;

    selected_effect = select_sound_effect(queue_bits);
    switch (selected_effect) {
    case Sfx_BowserFall:
        handle_square2_bowser_fall(is_start);
        break;
    case Sfx_CoinGrab:
    case Sfx_TimerTick:
        handle_square2_coin_or_timer(selected_effect, is_start);
        break;
    case Sfx_GrowPowerUp:
    case Sfx_GrowVine:
        handle_square2_growth(selected_effect, is_start);
        return;
    case Sfx_Blast:
        handle_square2_blast(is_start);
        break;
    case Sfx_PowerUpGrab:
        handle_square2_powerup_grab(is_start);
        break;
    case Sfx_ExtraLife:
        handle_square2_extra_life(is_start);
        break;
    default:
        break;
    }

    if (audio_state.square2_sfx_length_counter && --audio_state.square2_sfx_length_counter == 0)
        stop_square2();
}

static void handle_noise_sfx(void) {
    uint8_t queue_bits = g_NoiseSoundQueue ? g_NoiseSoundQueue : audio_state.noise_buffer;
    int is_start = g_NoiseSoundQueue != 0;

    if (!queue_bits)
        return;
    if (is_start)
        audio_state.noise_buffer = queue_bits;

    if (queue_bits & Sfx_BrickShatter) {
        if (is_start)
            audio_state.noise_sfx_length_counter = SFX_BRICK_SHATTER_LENGTH;

        {
            uint8_t table_index = (uint8_t)(audio_state.noise_sfx_length_counter >> 1);
            if ((audio_state.noise_sfx_length_counter & 1) &&
                table_index < sizeof(brick_shatter_frequency)) {
                uint8_t period = brick_shatter_frequency[table_index];
                uint8_t envelope =
                    read_music_rom((uint16_t)(MUSIC_BRICK_SHATTER_ENVELOPE + table_index));
                apu_write(APU_NOISE + APU_CONTROL_OFFSET, envelope);
                apu_write(APU_NOISE + APU_TIMER_LOW_OFFSET, period);
                apu_write(APU_NOISE + APU_TIMER_HIGH_OFFSET, APU_NOISE_LENGTH_SHORT);
            }
        }
    } else if (queue_bits & Sfx_BowserFlame) {
        /* ContinueBowserFlame (main.asm:13024-13030) uses the LSR result
         * only as the envelope-table index; unlike ContinueBrickShatter, it
         * does not branch on the carry or on a nonzero result.  Thus the final
         * counter value $01 still writes BowserFlameEnvData-1 ($400c=$93)
         * before the shared decrement emits the terminal $f0. */
        if (is_start)
            audio_state.noise_sfx_length_counter = SFX_BOWSER_FLAME_LENGTH;

        {
            uint8_t table_index = (uint8_t)(audio_state.noise_sfx_length_counter >> 1);
            uint8_t envelope =
                read_music_rom((uint16_t)(MUSIC_BOWSER_FLAME_ENVELOPE + table_index));
            apu_write(APU_NOISE + APU_CONTROL_OFFSET, envelope);
            apu_write(APU_NOISE + APU_TIMER_LOW_OFFSET, APU_NOISE_BOWSER_PERIOD);
            apu_write(APU_NOISE + APU_TIMER_HIGH_OFFSET, APU_NOISE_LENGTH_SHORT);
        }
    }

    if (audio_state.noise_sfx_length_counter && --audio_state.noise_sfx_length_counter == 0) {
        apu_write(APU_NOISE + APU_CONTROL_OFFSET, APU_NOISE_VOLUME_SILENT);
        audio_state.noise_buffer = 0;
    }
}

static uint8_t lookup_note_length(uint8_t encoded_index) {
    uint8_t table_index = (uint8_t)(encoded_index & MUSIC_LENGTH_INDEX_MASK);
    return read_music_rom((uint16_t)(MUSIC_LENGTH_TABLE + table_index +
                                     audio_state.note_length_table_offset +
                                     audio_state.note_length_table_adder));
}

static uint8_t lookup_alternate_length(uint8_t encoded_note) {
    /* AlternateLengthHandler rotates bits xx00000x into 00000xxx. */
    uint8_t length_index = (uint8_t)(((encoded_note & 1) << 2) | ((encoded_note >> 6) & 3));
    return lookup_note_length(length_index);
}

static uint8_t lookup_envelope(uint8_t envelope_index) {
    if (g_EventMusicBuffer & EndOfCastleMusic)
        return read_music_rom((uint16_t)(MUSIC_END_CASTLE_ENVELOPE + envelope_index));
    if ((audio_state.area_music_buffer & MUSIC_AREA_CONTROL_MASK) == 0)
        return read_music_rom((uint16_t)(MUSIC_WATER_EVENT_ENVELOPE + envelope_index));
    return read_music_rom((uint16_t)(MUSIC_AREA_ENVELOPE + envelope_index));
}

static uint8_t music_control_value(void) {
    if (g_EventMusicBuffer & EndOfCastleMusic)
        return MUSIC_CONTROL_END_CASTLE;
    return ((audio_state.area_music_buffer & MUSIC_AREA_CONTROL_MASK) == 0)
               ? MUSIC_CONTROL_WATER_EVENT
               : MUSIC_CONTROL_STANDARD;
}

static void load_music_header(uint8_t offset) {
    uint16_t header_address = (uint16_t)(SMB_MUSIC_ROM_BASE + offset);

    audio_state.note_length_table_offset = read_music_rom(header_address);
    audio_state.music_data_low = read_music_rom(++header_address);
    audio_state.music_data_high = read_music_rom(++header_address);
    audio_state.music_offset_triangle = read_music_rom(++header_address);
    audio_state.music_offset_square1 = read_music_rom(++header_address);
    audio_state.music_offset_noise = read_music_rom(++header_address);
    audio_state.noise_loopback_offset = audio_state.music_offset_noise;
    audio_state.square2_note_length_counter = 1;
    audio_state.square1_note_length_counter = 1;
    audio_state.triangle_note_length_counter = 1;
    audio_state.noise_beat_length_counter = 1;
    audio_state.music_offset_square2 = 0;
    audio_state.alternate_register_content_flag = 0;
    reload_apu_status(APU_STATUS_DISABLE_TRIANGLE, APU_STATUS_ENABLE_CHANNELS);
}

/* The ASM shifts the selected music bit until it finds the first set flag.
 * The resulting index addresses MusicHeaderOffsetData. */
static uint8_t music_header_index(uint8_t queue_bits, uint8_t initial_index) {
    uint8_t header_index = initial_index;
    do {
        ++header_index;
        if (queue_bits & 1)
            return header_index;
        queue_bits >>= 1;
    } while (queue_bits);
    return header_index;
}

static void load_event_music(uint8_t music_queue) {
    uint8_t header_index;

    g_EventMusicBuffer = music_queue;
    if (music_queue == DeathMusic) {
        stop_square1();
        silence_square2();
    }
    audio_state.area_music_buffer_alt = audio_state.area_music_buffer;
    audio_state.note_length_table_adder = 0;
    audio_state.area_music_buffer = 0;
    if (music_queue == TimeRunningOutMusic)
        audio_state.note_length_table_adder = 8;
    header_index = music_header_index(music_queue, 0);
    load_music_header(read_music_rom((uint16_t)(MUSIC_HEADER_OFFSET_TABLE + header_index)));
}

static void load_area_music(uint8_t music_queue, int is_new_queue) {
    uint8_t header_index;

    if (is_new_queue) {
        if (music_queue == UndergroundMusic)
            stop_square1();
        audio_state.ground_music_header_offset = MUSIC_GROUND_HEADER_START;
    }
    g_EventMusicBuffer = 0;
    audio_state.area_music_buffer = music_queue;
    if (music_queue == GroundMusic) {
        if (++audio_state.ground_music_header_offset == MUSIC_GROUND_HEADER_END)
            audio_state.ground_music_header_offset = MUSIC_GROUND_HEADER_LOOP;
        header_index = audio_state.ground_music_header_offset;
    } else
        header_index = music_header_index(music_queue, MUSIC_AREA_HEADER_INDEX_START);
    load_music_header(read_music_rom((uint16_t)(MUSIC_HEADER_OFFSET_TABLE + header_index)));
}
static void handle_music(void) {
    uint8_t music_queue;
    uint8_t stream_byte;
    uint8_t encoded_note;
    uint8_t control;

    if (g_EventMusicQueue)
        load_event_music(g_EventMusicQueue);
    else if (g_AreaMusicQueue)
        load_area_music(g_AreaMusicQueue, 1);
    else if (!(g_EventMusicBuffer | audio_state.area_music_buffer))
        return;

    /* MusicHandler first services square 2.  A song terminator can reload a
     * header and restart this section, so this loop is intentional. */
    for (;;) {
        if (--audio_state.square2_note_length_counter == 0) {
            stream_byte = read_music_rom(
                (uint16_t)(music_data_pointer() + audio_state.music_offset_square2++));
            if (!stream_byte) {
                if (g_EventMusicBuffer == TimeRunningOutMusic &&
                    audio_state.area_music_buffer_alt) {
                    load_area_music(audio_state.area_music_buffer_alt, 0);
                    continue;
                }
                if (g_EventMusicBuffer & VictoryMusic) {
                    music_queue = g_EventMusicBuffer;
                    load_event_music(music_queue);
                    continue;
                }
                if (audio_state.area_music_buffer & MUSIC_AREA_LOOP_MASK) {
                    music_queue = audio_state.area_music_buffer;
                    load_area_music(music_queue, 0);
                    continue;
                }
                audio_state.area_music_buffer = g_EventMusicBuffer = 0;
                apu_write(APU_TRIANGLE + APU_CONTROL_OFFSET, 0);
                apu_write(APU_SQUARE1 + APU_CONTROL_OFFSET, APU_SQUARE_VOLUME_SILENT);
                apu_write(APU_SQUARE2 + APU_CONTROL_OFFSET, APU_SQUARE_VOLUME_SILENT);
                return;
            }
            if (stream_byte & MUSIC_LENGTH_FLAG) {
                audio_state.square2_note_length_buffer = lookup_note_length(stream_byte);
                stream_byte = read_music_rom(
                    (uint16_t)(music_data_pointer() + audio_state.music_offset_square2++));
            }
            if (!audio_state.square2_buffer) {
                uint8_t timer_low = set_frequency(APU_SQUARE2, stream_byte);
                control = timer_low ? music_control_value() : 0;
                audio_state.square2_envelope = control;
                if (timer_low) {
                    apu_write(APU_SQUARE2 + APU_CONTROL_OFFSET, APU_SQUARE_VOLUME_MUSIC);
                    apu_write(APU_SQUARE2 + APU_SWEEP_OFFSET, APU_SQUARE_SWEEP_DEFAULT);
                } else {
                    apu_write(APU_SQUARE2 + APU_CONTROL_OFFSET, APU_SQUARE_VOLUME_MUSIC_REST);
                    apu_write(APU_SQUARE2 + APU_SWEEP_OFFSET, stream_byte);
                }
            }
            audio_state.square2_note_length_counter = audio_state.square2_note_length_buffer;
        }
        break;
    }

    if (!audio_state.square2_buffer && !(g_EventMusicBuffer & MUSIC_ENVELOPE_SKIP_MASK)) {
        uint8_t envelope_index = audio_state.square2_envelope;
        if (audio_state.square2_envelope)
            --audio_state.square2_envelope;
        apu_write(APU_SQUARE2 + APU_CONTROL_OFFSET, lookup_envelope(envelope_index));
        apu_write(APU_SQUARE2 + APU_SWEEP_OFFSET, APU_SQUARE_SWEEP_DEFAULT);
    }

    if (audio_state.music_offset_square1) {
        if (--audio_state.square1_note_length_counter == 0) {
            do {
                stream_byte = read_music_rom(
                    (uint16_t)(music_data_pointer() + audio_state.music_offset_square1++));
                if (!stream_byte) {
                    apu_write(APU_SQUARE1 + APU_CONTROL_OFFSET, APU_SQUARE1_VOLUME_STREAM_END);
                    apu_write(APU_SQUARE1 + APU_SWEEP_OFFSET, APU_SQUARE1_SWEEP_STREAM_END);
                    audio_state.alternate_register_content_flag = APU_SQUARE1_SWEEP_STREAM_END;
                }
            } while (!stream_byte);
            audio_state.square1_note_length_counter = lookup_alternate_length(stream_byte);
            if (!audio_state.square1_buffer) {
                uint8_t timer_low;
                encoded_note = (uint8_t)(stream_byte & MUSIC_NOTE_MASK);
                timer_low = set_frequency(APU_SQUARE1, encoded_note);
                control = timer_low ? music_control_value() : 0;
                audio_state.square1_envelope = control;
                if (timer_low) {
                    apu_write(APU_SQUARE1 + APU_SWEEP_OFFSET, APU_SQUARE_SWEEP_DEFAULT);
                    apu_write(APU_SQUARE1 + APU_CONTROL_OFFSET, APU_SQUARE_VOLUME_MUSIC);
                } else {
                    apu_write(APU_SQUARE1 + APU_SWEEP_OFFSET, encoded_note);
                    apu_write(APU_SQUARE1 + APU_CONTROL_OFFSET, MUSIC_SQUARE_INVALID_VOLUME);
                }
            }
        }
        if (!audio_state.square1_buffer) {
            if (!(g_EventMusicBuffer & MUSIC_ENVELOPE_SKIP_MASK)) {
                uint8_t envelope_index = audio_state.square1_envelope;
                if (audio_state.square1_envelope)
                    --audio_state.square1_envelope;
                apu_write(APU_SQUARE1 + APU_CONTROL_OFFSET, lookup_envelope(envelope_index));
            }
            apu_write(APU_SQUARE1 + APU_SWEEP_OFFSET,
                      audio_state.alternate_register_content_flag
                          ? audio_state.alternate_register_content_flag
                          : APU_SQUARE_SWEEP_DEFAULT);
        }
    }

    if (--audio_state.triangle_note_length_counter == 0) {
        stream_byte =
            read_music_rom((uint16_t)(music_data_pointer() + audio_state.music_offset_triangle++));
        if (stream_byte & MUSIC_LENGTH_FLAG) {
            audio_state.triangle_note_length_buffer = lookup_note_length(stream_byte);
            apu_write(APU_TRIANGLE + APU_CONTROL_OFFSET, APU_TRIANGLE_CONTROL_SHORT);
            stream_byte = read_music_rom(
                (uint16_t)(music_data_pointer() + audio_state.music_offset_triangle++));
        }
        if (stream_byte) {
            set_frequency(APU_TRIANGLE, stream_byte);
            audio_state.triangle_note_length_counter = audio_state.triangle_note_length_buffer;
            if ((g_EventMusicBuffer & MUSIC_TRIANGLE_EVENT_MASK) ||
                (audio_state.area_music_buffer & MUSIC_TRIANGLE_AREA_MASK)) {
                control =
                    (audio_state.triangle_note_length_counter >= MUSIC_LONG_TRIANGLE_NOTE)
                        ? APU_TRIANGLE_CONTROL_LONG
                        : ((g_EventMusicBuffer & EndOfCastleMusic) ? APU_TRIANGLE_CONTROL_CASTLE
                                                                   : APU_TRIANGLE_CONTROL_SHORT);
                apu_write(APU_TRIANGLE + APU_CONTROL_OFFSET, control);
            }
        } else {
            apu_write(APU_TRIANGLE + APU_CONTROL_OFFSET, 0);
        }
    }

    if (audio_state.area_music_buffer & MUSIC_NOISE_AREA_MASK) {
        if (--audio_state.noise_beat_length_counter == 0) {
            do {
                stream_byte = read_music_rom(
                    (uint16_t)(music_data_pointer() + audio_state.music_offset_noise++));
                if (!stream_byte)
                    audio_state.music_offset_noise = audio_state.noise_loopback_offset;
            } while (!stream_byte);
            audio_state.noise_beat_length_counter = lookup_alternate_length(stream_byte);
            encoded_note = (uint8_t)(stream_byte & MUSIC_NOTE_MASK);
            if (encoded_note == MUSIC_NOISE_LONG_BEAT) {
                apu_write(APU_NOISE + APU_CONTROL_OFFSET, APU_NOISE_VOLUME_MUSIC);
                apu_write(APU_NOISE + APU_TIMER_LOW_OFFSET, MUSIC_NOISE_LONG_PERIOD);
                apu_write(APU_NOISE + APU_TIMER_HIGH_OFFSET, APU_NOISE_LENGTH_LONG);
            } else if (encoded_note == MUSIC_NOISE_STRONG_BEAT) {
                apu_write(APU_NOISE + APU_CONTROL_OFFSET, APU_NOISE_VOLUME_MUSIC);
                apu_write(APU_NOISE + APU_TIMER_LOW_OFFSET, MUSIC_NOISE_STRONG_PERIOD);
                apu_write(APU_NOISE + APU_TIMER_HIGH_OFFSET, APU_NOISE_LENGTH_SHORT);
            } else if (encoded_note & MUSIC_NOISE_SHORT_BEAT) {
                apu_write(APU_NOISE + APU_CONTROL_OFFSET, APU_NOISE_VOLUME_MUSIC);
                apu_write(APU_NOISE + APU_TIMER_LOW_OFFSET, MUSIC_NOISE_SHORT_PERIOD);
                apu_write(APU_NOISE + APU_TIMER_HIGH_OFFSET, APU_NOISE_LENGTH_SHORT);
            } else {
                uint8_t length_index =
                    (uint8_t)(((stream_byte & 1) << 2) | ((stream_byte >> 6) & 3));
                length_index = (uint8_t)(length_index + audio_state.note_length_table_offset +
                                         audio_state.note_length_table_adder);
                apu_write(APU_NOISE + APU_CONTROL_OFFSET, APU_NOISE_VOLUME_GENERIC);
                apu_write(APU_NOISE + APU_TIMER_LOW_OFFSET, stream_byte);
                apu_write(APU_NOISE + APU_TIMER_HIGH_OFFSET, length_index);
            }
        }
    }
}

static void handle_pause_sound(void) {
    uint8_t note_index;

    if (!audio_state.pause_buffer) {
        if (!g_PauseSoundQueue)
            return;
        audio_state.pause_buffer = g_PauseSoundQueue;
        audio_state.pause_mode_flag = g_PauseSoundQueue;
        apu_write(APU_STATUS, 0);
        audio_state.square1_buffer = audio_state.square2_buffer = audio_state.noise_buffer = 0;
        apu_write(APU_STATUS, APU_STATUS_ENABLE_CHANNELS);
        audio_state.square1_sfx_length_counter = PAUSE_SOUND_LENGTH;
    }

    note_index = (audio_state.square1_sfx_length_counter == PAUSE_SECOND_TONE_LATE ||
                  audio_state.square1_sfx_length_counter == PAUSE_SECOND_TONE_EARLY)
                     ? PAUSE_SECOND_TONE_NOTE_INDEX
                     : PAUSE_FIRST_TONE_NOTE_INDEX;
    if (audio_state.square1_sfx_length_counter == PAUSE_SOUND_LENGTH ||
        audio_state.square1_sfx_length_counter == PAUSE_SECOND_TONE_LATE ||
        audio_state.square1_sfx_length_counter == PAUSE_FIRST_TONE_REPEAT ||
        audio_state.square1_sfx_length_counter == PAUSE_SECOND_TONE_EARLY)
        play_square1(APU_PAUSE_VOLUME, APU_SQUARE_SWEEP_DEFAULT, note_index);

    if (audio_state.square1_sfx_length_counter && --audio_state.square1_sfx_length_counter == 0) {
        apu_write(APU_STATUS, 0);
        if (audio_state.pause_buffer == 2)
            audio_state.pause_mode_flag = 0;
        audio_state.pause_buffer = 0;
    }
}

void Audio_SoundEngine(void) {
    uint8_t old_dac = audio_state.dac_counter;

    if (g_OperMode == TITLE_SCREEN_MODE) {
        apu_write(APU_STATUS, 0);
        return;
    }
    apu_write(APU_FRAME_COUNTER, APU_FRAME_COUNTER_RELOAD);
    apu_write(APU_STATUS, APU_STATUS_ENABLE_CHANNELS);
    if (audio_state.pause_mode_flag || g_PauseSoundQueue == 1) {
        handle_pause_sound();
    } else {
        handle_square1_sfx();
        handle_square2_sfx();
        handle_noise_sfx();
        handle_music();
        g_AreaMusicQueue = 0;
        g_EventMusicQueue = 0;
    }
    g_Square1SoundQueue = g_Square2SoundQueue = g_NoiseSoundQueue = g_PauseSoundQueue = 0;
    if (audio_state.area_music_buffer & MUSIC_DAC_AREA_MASK) {
        if (audio_state.dac_counter < DAC_COUNTER_LIMIT)
            ++audio_state.dac_counter;
    } else if (audio_state.dac_counter) {
        --audio_state.dac_counter;
    }
    apu_write(APU_DMC + APU_DMC_OUTPUT_OFFSET, old_dac);
}

void Audio_GetVerifierState(AudioVerifierState *state) {
    memset(state, 0, sizeof(*state));
    state->note_table_offset = audio_state.note_length_table_offset;
    state->square1_buffer = audio_state.square1_buffer;
    state->square2_buffer = audio_state.square2_buffer;
    state->noise_buffer = audio_state.noise_buffer;
    state->area_buffer = audio_state.area_music_buffer;
    state->music_data_low = audio_state.music_data_low;
    state->music_data_high = audio_state.music_data_high;
    state->music_offset_square2 = audio_state.music_offset_square2;
    state->music_offset_square1 = audio_state.music_offset_square1;
    state->music_offset_triangle = audio_state.music_offset_triangle;
    state->pause_queue = g_PauseSoundQueue;
    state->area_music_queue = g_AreaMusicQueue;
    state->event_music_queue = g_EventMusicQueue;
    state->noise_queue = g_NoiseSoundQueue;
    state->square2_queue = g_Square2SoundQueue;
    state->square1_queue = g_Square1SoundQueue;
    state->music_offset_noise = audio_state.music_offset_noise;
    state->event_music_buffer = g_EventMusicBuffer;
    state->pause_buffer = audio_state.pause_buffer;
    state->square2_note_length_buffer = audio_state.square2_note_length_buffer;
    state->square2_note_length_counter = audio_state.square2_note_length_counter;
    state->square2_envelope = audio_state.square2_envelope;
    state->square1_note_length_counter = audio_state.square1_note_length_counter;
    state->square1_envelope = audio_state.square1_envelope;
    state->triangle_note_length_buffer = audio_state.triangle_note_length_buffer;
    state->triangle_note_length_counter = audio_state.triangle_note_length_counter;
    state->noise_beat_length_counter = audio_state.noise_beat_length_counter;
    state->square1_sfx_length_counter = audio_state.square1_sfx_length_counter;
    state->square2_sfx_length_counter = audio_state.square2_sfx_length_counter;
    state->sfx_secondary_counter = audio_state.sfx_secondary_counter;
    state->noise_sfx_length_counter = audio_state.noise_sfx_length_counter;
    state->dac_counter = audio_state.dac_counter;
    state->noise_loopback_offset = audio_state.noise_loopback_offset;
    state->note_length_table_adder = audio_state.note_length_table_adder;
    state->area_music_buffer_alt = audio_state.area_music_buffer_alt;
    state->pause_mode_flag = audio_state.pause_mode_flag;
    state->ground_music_header_offset = audio_state.ground_music_header_offset;
    state->alternate_register_content_flag = audio_state.alternate_register_content_flag;
}
