/* ASM-first SMB sound driver. The game-specific driver is kept separate from
 * the host 2A03 renderer so NES builds can write the same registers directly. */
#include <string.h>

#include "audio.h"
#include "assets.h"

typedef char AudioVerifierState_must_be_38_bytes[
    sizeof(AudioVerifierState) == 38 ? 1 : -1];
#include "audio-data.h"
#include "constants/defs.h"
#include "constants/globals.h"
#include "system/platform.h"

enum {
    SQ1 = 0x4000, SQ2 = 0x4004, TRI = 0x4008, NOISE = 0x400c,
    DAC = 0x4010, STATUS = 0x4015, FRAME = 0x4017
};

typedef struct AudioState {
    uint8_t sq1_buf, sq2_buf, noise_buf, area_buf, pause_buf;
    uint8_t music_data_lo, music_data_hi;
    uint8_t off_sq2, off_sq1, off_tri, off_noise;
    uint8_t note_table, sq2_len_buf, sq2_len, sq2_env;
    uint8_t sq1_len, sq1_env, tri_len_buf, tri_len, noise_len;
    uint8_t sq1_sfx_len, sq2_sfx_len, secondary, noise_sfx_len;
    uint8_t dac_counter, noise_loop, note_adder, area_alt;
    uint8_t pause_mode, ground_header, alt_reg;
} AudioState;

static AudioState a;

/* InitializeMemory (main.asm:1536-1555) is called with Y=$4b by
 * InitializeArea.  Its page loop clears the sound symbols at $00f0-$00ff,
 * but it does not reach SoundMemory at $07b0.  Keep the high sound bank
 * untouched here, just as the 6502 loop does. */
void Audio_ResetLowMemory(void) {
    a.note_table = 0;       /* NoteLenLookupTblOfs=$00f0 */
    a.sq1_buf = 0;           /* Square1SoundBuffer=$00f1 */
    a.sq2_buf = 0;           /* Square2SoundBuffer=$00f2 */
    a.noise_buf = 0;         /* NoiseSoundBuffer=$00f3 */
    a.area_buf = 0;          /* AreaMusicBuffer=$00f4 */
    a.music_data_lo = 0;    /* MusicDataLow=$00f5 */
    a.music_data_hi = 0;    /* MusicDataHigh=$00f6 */
    a.off_sq2 = 0;           /* MusicOffset_Square2=$00f7 */
    a.off_sq1 = 0;           /* MusicOffset_Square1=$00f8 */
    a.off_tri = 0;           /* MusicOffset_Triangle=$00f9 */

    g_PauseSoundQueue = 0;   /* PauseSoundQueue=$00fa */
    g_AreaMusicQueue = 0;   /* AreaMusicQueue=$00fb */
    g_EventMusicQueue = 0;  /* EventMusicQueue=$00fc */
    g_NoiseSoundQueue = 0;  /* NoiseSoundQueue=$00fd */
    g_Square2SoundQueue = 0;/* Square2SoundQueue=$00fe */
    g_Square1SoundQueue = 0;/* Square1SoundQueue=$00ff */
}

/* InitializeGame performs the same low-page clear and then ClrSndLoop
 * (main.asm:1405-1412) over SoundMemory=$07b0-$07cf.  The full struct is
 * the C owner of those physical sound bytes; the queue globals are the
 * corresponding $00f0-$00ff aliases cleared by InitializeMemory. */
void Audio_ResetGameMemory(void) {
    memset(&a, 0, sizeof(a));
    g_EventMusicBuffer = 0; /* EventMusicBuffer=$07b1, outside AudioState. */
    g_PauseSoundQueue = 0;
    g_AreaMusicQueue = 0;
    g_EventMusicQueue = 0;
    g_NoiseSoundQueue = 0;
    g_Square2SoundQueue = 0;
    g_Square1SoundQueue = 0;
}

static uint8_t swim_env[14];
static uint8_t extra_life_freq[6];
static uint8_t powerup_freq[27];
static uint8_t grow_freq[32];
static uint8_t brick_freq[16];

int Audio_LoadRomTables(void) {
    if (Assets_Copy("tables/swim_stomp_envelope.bin", swim_env, sizeof(swim_env)) ||
        Assets_Copy("tables/extra_life_freq.bin", extra_life_freq,
                    sizeof(extra_life_freq)) ||
        Assets_Copy("tables/powerup_grab_freq.bin", powerup_freq,
                    sizeof(powerup_freq)) ||
        Assets_Copy("tables/powerup_vgrow_freq.bin", grow_freq, sizeof(grow_freq)) ||
        Assets_Copy("tables/brick_shatter_freq.bin", brick_freq, sizeof(brick_freq)))
        return -1;
    return 0;
}

static uint8_t rom(uint16_t address) {
    unsigned int offset;
    if (address < SMB_MUSIC_ROM_BASE) return 0;
    offset = (unsigned int)(address - SMB_MUSIC_ROM_BASE);
    return offset < smb_music_rom_len ? smb_music_rom[offset] : 0;
}
static uint16_t music_ptr(void) {
    return (uint16_t)((uint16_t)a.music_data_hi << 8) | a.music_data_lo;
}
static void wr(uint16_t address, uint8_t value) { platform_apu_write(address, value); }
static void status_reload(uint8_t off, uint8_t on) { wr(STATUS, off); wr(STATUS, on); }

static uint8_t set_freq(uint16_t base, uint8_t offset) {
    uint8_t lo = rom((uint16_t)(0xff01u + offset));
    if (!lo) return 0;
    wr((uint16_t)(base + 2), lo);
    wr((uint16_t)(base + 3), (uint8_t)(rom((uint16_t)(0xff00u + offset)) | 8));
    return lo;
}
static void play_sq1(uint8_t vol, uint8_t sweep, uint8_t freq) {
    wr(SQ1 + 1, sweep); wr(SQ1, vol); set_freq(SQ1, freq);
}
static void play_sq2(uint8_t vol, uint8_t sweep, uint8_t freq) {
    wr(SQ2, vol); wr(SQ2 + 1, sweep); set_freq(SQ2, freq);
}
static void stop_sq1(void) { a.sq1_buf = 0; status_reload(0x0e, 0x0f); }
static void stop_sq2(void) { a.sq2_buf = 0; status_reload(0x0d, 0x0f); }
static void silence_sq2(void) { status_reload(0x0d, 0x0f); }
static uint8_t sfx_dispatch_bit(uint8_t bits) {
    uint8_t bit;
    if (bits & 0x80) return 0x80;
    bit = (uint8_t)(bits & (uint8_t)(0u - bits));
    return bit;
}

static void square1_sfx(void) {
    uint8_t b = g_Square1SoundQueue ? g_Square1SoundQueue : a.sq1_buf;
    uint8_t selected;
    int start = g_Square1SoundQueue != 0;
    if (!b) return;
    if (start) a.sq1_buf = b;
    selected = sfx_dispatch_bit(b);

    if (selected == Sfx_SmallJump || selected == Sfx_BigJump) {
        if (start) { play_sq1(0x82,0xa7,selected == Sfx_SmallJump?0x26:0x18); a.sq1_sfx_len=0x28; }
        else {
            if (a.sq1_sfx_len==0x25) { wr(SQ1+1,0xf6); wr(SQ1,0x5f); }
            else if (a.sq1_sfx_len==0x20) { wr(SQ1+1,0xbc); wr(SQ1,0x48); }
        }
    } else if (selected == Sfx_Bump || selected == Sfx_Fireball) {
        if (start) { play_sq1(0x9e,selected == Sfx_Bump?0x93:0x99,0x0c); a.sq1_sfx_len=selected == Sfx_Bump?0x0a:0x05; }
        else if (a.sq1_sfx_len==0x06) wr(SQ1+1,0xbb);
    } else if (selected == Sfx_EnemyStomp) {
        if (start) { play_sq1(0x9e,0x9c,0x26); a.sq1_sfx_len=0x0e; }
        { uint8_t n=a.sq1_sfx_len; if(n && n<=14) wr(SQ1,swim_env[n-1]); if(n==6) wr(SQ1+2,0x9e); }
    } else if (selected == Sfx_EnemySmack) {
        if (start) { play_sq1(0x9f,0xcb,0x28); a.sq1_sfx_len=0x0e; }
        else { if(a.sq1_sfx_len==8) wr(SQ1+2,0xa0); wr(SQ1,a.sq1_sfx_len==8?0x9f:0x90); }
    } else if (selected == Sfx_PipeDown_Injury) {
        if (start) a.sq1_sfx_len=0x2f;
        if ((((a.sq1_sfx_len >> 2) & 2) != 0) && !(a.sq1_sfx_len & 3))
            play_sq1(0x9a,0x91,0x44);
    } else if (selected == Sfx_Flagpole) {
        if (start) { a.sq1_sfx_len=0x40; set_freq(SQ1,0x62); wr(SQ1+1,0xbc); wr(SQ1,0x99); }
    }
    if (a.sq1_sfx_len && --a.sq1_sfx_len==0) stop_sq1();
}

static void square2_sfx(void) {
    uint8_t b, selected;
    int start;
    if (a.sq2_buf & Sfx_ExtraLife) { b=a.sq2_buf; start=0; }
    else { b=g_Square2SoundQueue ? g_Square2SoundQueue : a.sq2_buf; start=g_Square2SoundQueue!=0; }
    if (!b) return;
    if (start) a.sq2_buf=b;
    selected=sfx_dispatch_bit(b);

    if (selected == Sfx_BowserFall) {
        if(start){a.sq2_sfx_len=0x38;play_sq2(0x9f,0xc4,0x18);}
        else if(a.sq2_sfx_len==8) play_sq2(0x9f,0xa4,0x5a);
    } else if (selected == Sfx_CoinGrab || selected == Sfx_TimerTick) {
        if(start){a.sq2_sfx_len=selected == Sfx_CoinGrab?0x35:0x06;play_sq2(selected == Sfx_CoinGrab?0x8d:0x98,0x7f,0x42);}
        else if(a.sq2_sfx_len==0x30) wr(SQ2+2,0x54);
    } else if (selected == Sfx_GrowPowerUp || selected == Sfx_GrowVine) {
        if(start){a.sq2_sfx_len=selected == Sfx_GrowPowerUp?0x10:0x20;a.secondary=0;wr(SQ2+1,0x7f);}
        ++a.secondary;
        { uint8_t y=(uint8_t)(a.secondary>>1); if(y==a.sq2_sfx_len){stop_sq2();return;} wr(SQ2,0x9d);set_freq(SQ2,grow_freq[y]); }
        return;
    } else if (selected == Sfx_Blast) {
        if(start){a.sq2_sfx_len=0x20;play_sq2(0x9f,0x94,0x5e);}
        else if(a.sq2_sfx_len==0x18) play_sq2(0x9f,0x93,0x18);
    } else if (selected == Sfx_PowerUpGrab) {
        if(start)a.sq2_sfx_len=0x36;
        if(!(a.sq2_sfx_len&1)){uint8_t y=(uint8_t)(a.sq2_sfx_len>>1);if(y&&y<=27)play_sq2(0x5d,0x7f,powerup_freq[y-1]);}
    } else if (selected == Sfx_ExtraLife) {
        if(start)a.sq2_sfx_len=0x30;
        if(!(a.sq2_sfx_len&7)){uint8_t y=(uint8_t)(a.sq2_sfx_len>>3);if(y&&y<=6)play_sq2(0x82,0x7f,extra_life_freq[y-1]);}
    }
    if(a.sq2_sfx_len && --a.sq2_sfx_len==0) stop_sq2();
}

static void noise_sfx(void) {
    uint8_t b=g_NoiseSoundQueue?g_NoiseSoundQueue:a.noise_buf;
    int start=g_NoiseSoundQueue!=0;
    uint8_t y, env, period;
    if(!b)return;
    if(start)a.noise_buf=b;
    if(b&Sfx_BrickShatter){if(start)a.noise_sfx_len=0x20;y=(uint8_t)(a.noise_sfx_len>>1);if((a.noise_sfx_len&1)&&y<16){period=brick_freq[y];env=rom((uint16_t)(0xffeau+y));wr(NOISE,env);wr(NOISE+2,period);wr(NOISE+3,0x18);}}
    /* ContinueBowserFlame (main.asm:13024-13030) uses the LSR result only
     * as the envelope-table index; unlike ContinueBrickShatter, it does not
     * branch on the carry or on a nonzero result.  Thus the final counter
     * value $01 still writes BowserFlameEnvData-1 ($400c=$93) before the
     * shared decrement emits the terminal $f0. */
    else if(b&Sfx_BowserFlame){if(start)a.noise_sfx_len=0x40;y=(uint8_t)(a.noise_sfx_len>>1);env=rom((uint16_t)(0xffc9u+y));wr(NOISE,env);wr(NOISE+2,0x0f);wr(NOISE+3,0x18);}
    if(a.noise_sfx_len&&--a.noise_sfx_len==0){wr(NOISE,0xf0);a.noise_buf=0;}
}

static uint8_t note_length(uint8_t index) {
    return rom((uint16_t)(0xff66u + ((index&7)+a.note_table+a.note_adder)));
}
static uint8_t alternate_length(uint8_t value) {
    uint8_t index=(uint8_t)(((value&1)<<2)|((value>>6)&3));
    return note_length(index);
}
static uint8_t envelope(uint8_t y) {
    if(g_EventMusicBuffer&EndOfCastleMusic)return rom((uint16_t)(0xff96u+y));
    if((a.area_buf&0x7d)==0)return rom((uint16_t)(0xffa2u+y));
    return rom((uint16_t)(0xff9au+y));
}
static uint8_t control_value(void) {
    if(g_EventMusicBuffer&EndOfCastleMusic)return 4;
    return ((a.area_buf&0x7d)==0)?0x28:8;
}

static void load_header(uint8_t offset) {
    uint16_t p=(uint16_t)(SMB_MUSIC_ROM_BASE+offset);
    a.note_table=rom(p);a.music_data_lo=rom(++p);a.music_data_hi=rom(++p);
    a.off_tri=rom(++p);a.off_sq1=rom(++p);a.off_noise=rom(++p);a.noise_loop=a.off_noise;
    a.sq2_len=a.sq1_len=a.tri_len=a.noise_len=1;a.off_sq2=0;a.alt_reg=0;
    status_reload(0x0b,0x0f);
}
static uint8_t header_index(uint8_t queue,uint8_t y) {
    do{++y;if(queue&1)return y;queue>>=1;}while(queue);return y;
}
static void load_event_music(uint8_t q) {
    uint8_t y=0;
    g_EventMusicBuffer=q;
    if(q==DeathMusic){stop_sq1();silence_sq2();}
    a.area_alt=a.area_buf;a.note_adder=0;a.area_buf=0;
    if(q==TimeRunningOutMusic)a.note_adder=8;
    y=header_index(q,y);
    load_header(rom((uint16_t)(0xf90cu+y)));
}
static void load_area_music(uint8_t q, int fresh_queue) {
    uint8_t y;
    if(fresh_queue){
        if(q==UndergroundMusic)stop_sq1();
        a.ground_header=0x10;
    }
    g_EventMusicBuffer=0;a.area_buf=q;
    if(q==GroundMusic){
        if(++a.ground_header==0x32)a.ground_header=0x11;
        y=a.ground_header;
    }else y=header_index(q,8);
    load_header(rom((uint16_t)(0xf90cu+y)));
}
static void music_handler(void) {
    uint8_t q, byte, x, ctrl;

    if (g_EventMusicQueue)
        load_event_music(g_EventMusicQueue);
    else if (g_AreaMusicQueue)
        load_area_music(g_AreaMusicQueue, 1);
    else if (!(g_EventMusicBuffer | a.area_buf))
        return;

    for (;;) {
        if (--a.sq2_len == 0) {
            byte = rom((uint16_t)(music_ptr() + a.off_sq2++));
            if (!byte) {
                if (g_EventMusicBuffer == TimeRunningOutMusic && a.area_alt) {
                    load_area_music(a.area_alt, 0);
                    continue;
                }
                if (g_EventMusicBuffer & VictoryMusic) {
                    q = g_EventMusicBuffer;
                    load_event_music(q);
                    continue;
                }
                if (a.area_buf & 0x5f) {
                    q = a.area_buf;
                    load_area_music(q, 0);
                    continue;
                }
                a.area_buf = g_EventMusicBuffer = 0;
                wr(TRI, 0);
                wr(SQ1, 0x90);
                wr(SQ2, 0x90);
                return;
            }
            if (byte & 0x80) {
                a.sq2_len_buf = note_length(byte);
                byte = rom((uint16_t)(music_ptr() + a.off_sq2++));
            }
            if (!a.sq2_buf) {
                uint8_t tone = set_freq(SQ2, byte);
                ctrl = tone ? control_value() : 0;
                a.sq2_env = ctrl;
                if (tone) {
                    wr(SQ2, 0x82);
                    wr(SQ2 + 1, 0x7f);
                } else {
                    wr(SQ2, 0x04);
                    wr(SQ2 + 1, byte);
                }
            }
            a.sq2_len = a.sq2_len_buf;
        }
        break;
    }

    if (!a.sq2_buf && !(g_EventMusicBuffer & 0x91)) {
        uint8_t env_index = a.sq2_env;
        if (a.sq2_env)
            --a.sq2_env;
        wr(SQ2, envelope(env_index));
        wr(SQ2 + 1, 0x7f);
    }

    if (a.off_sq1) {
        if (--a.sq1_len == 0) {
            do {
                byte = rom((uint16_t)(music_ptr() + a.off_sq1++));
                if (!byte) {
                    wr(SQ1, 0x83);
                    wr(SQ1 + 1, 0x94);
                    a.alt_reg = 0x94;
                }
            } while (!byte);
            a.sq1_len = alternate_length(byte);
            if (!a.sq1_buf) {
                uint8_t tone;
                x = (uint8_t)(byte & 0x3e);
                tone = set_freq(SQ1, x);
                ctrl = tone ? control_value() : 0;
                a.sq1_env = ctrl;
                if (tone) {
                    wr(SQ1 + 1, 0x7f);
                    wr(SQ1, 0x82);
                } else {
                    wr(SQ1 + 1, x);
                    wr(SQ1, 0);
                }
            }
        }
        if (!a.sq1_buf) {
            if (!(g_EventMusicBuffer & 0x91)) {
                uint8_t env_index = a.sq1_env;
                if (a.sq1_env)
                    --a.sq1_env;
                wr(SQ1, envelope(env_index));
            }
            wr(SQ1 + 1, a.alt_reg ? a.alt_reg : 0x7f);
        }
    }

    if (--a.tri_len == 0) {
        byte = rom((uint16_t)(music_ptr() + a.off_tri++));
        if (byte & 0x80) {
            a.tri_len_buf = note_length(byte);
            wr(TRI, 0x1f);
            byte = rom((uint16_t)(music_ptr() + a.off_tri++));
        }
        if (byte) {
            set_freq(TRI, byte);
            a.tri_len = a.tri_len_buf;
            if ((g_EventMusicBuffer & 0x6e) || (a.area_buf & 0x0a)) {
                ctrl = (a.tri_len >= 0x12) ? 0xff
                    : ((g_EventMusicBuffer & EndOfCastleMusic) ? 0x0f : 0x1f);
                wr(TRI, ctrl);
            }
        } else {
            wr(TRI, 0);
        }
    }

    if ((a.area_buf & 0xf3) != 0) {
        if (--a.noise_len == 0) {
            do {
                byte = rom((uint16_t)(music_ptr() + a.off_noise++));
                if (!byte)
                    a.off_noise = a.noise_loop;
            } while (!byte);
            a.noise_len = alternate_length(byte);
            x = (uint8_t)(byte & 0x3e);
            if (x == 0x30) {
                wr(NOISE, 0x1c);
                wr(NOISE + 2, 3);
                wr(NOISE + 3, 0x58);
            } else if (x == 0x20) {
                wr(NOISE, 0x1c);
                wr(NOISE + 2, 0x0c);
                wr(NOISE + 3, 0x18);
            } else if (x & 0x10) {
                wr(NOISE, 0x1c);
                wr(NOISE + 2, 3);
                wr(NOISE + 3, 0x18);
            } else {
                uint8_t length_index = (uint8_t)(((byte & 1) << 2) |
                                                 ((byte >> 6) & 3));
                length_index = (uint8_t)(length_index + a.note_table +
                                         a.note_adder);
                wr(NOISE, 0x10);
                wr(NOISE + 2, byte);
                wr(NOISE + 3, length_index);
            }
        }
    }
}

static void pause_sound(void) {
    uint8_t tone;
    if (!a.pause_buf) {
        if (!g_PauseSoundQueue)
            return;
        a.pause_buf = g_PauseSoundQueue;
        a.pause_mode = g_PauseSoundQueue;
        wr(STATUS, 0);
        a.sq1_buf = a.sq2_buf = a.noise_buf = 0;
        wr(STATUS, 0x0f);
        a.sq1_sfx_len = 0x2a;
    }
    tone = (a.sq1_sfx_len == 0x24 || a.sq1_sfx_len == 0x18) ? 0x64 : 0x44;
    if (a.sq1_sfx_len == 0x2a || a.sq1_sfx_len == 0x24 ||
        a.sq1_sfx_len == 0x1e || a.sq1_sfx_len == 0x18)
        play_sq1(0x84, 0x7f, tone);
    if (a.sq1_sfx_len && --a.sq1_sfx_len == 0) {
        wr(STATUS, 0);
        if (a.pause_buf == 2)
            a.pause_mode = 0;
        a.pause_buf = 0;
    }
}

void Audio_SoundEngine(void) {
    uint8_t old_dac = a.dac_counter;
    if (g_OperMode == TITLE_SCREEN_MODE) {
        wr(STATUS, 0);
        return;
    }
    wr(FRAME, 0xff);
    wr(STATUS, 0x0f);
    if (a.pause_mode || g_PauseSoundQueue == 1) {
        pause_sound();
    } else {
        square1_sfx();
        square2_sfx();
        noise_sfx();
        music_handler();
        g_AreaMusicQueue = 0;
        g_EventMusicQueue = 0;
    }
    g_Square1SoundQueue = g_Square2SoundQueue = g_NoiseSoundQueue =
        g_PauseSoundQueue = 0;
    if (a.area_buf & 3) {
        if (a.dac_counter < 0x30)
            ++a.dac_counter;
    } else if (a.dac_counter) {
        --a.dac_counter;
    }
    wr(DAC + 1, old_dac);
}

void Audio_GetVerifierState(AudioVerifierState *s) {
    memset(s, 0, sizeof(*s));
    s->note_table_offset = a.note_table;
    s->square1_buffer = a.sq1_buf; s->square2_buffer = a.sq2_buf;
    s->noise_buffer = a.noise_buf; s->area_buffer = a.area_buf;
    s->music_data_low = a.music_data_lo; s->music_data_high = a.music_data_hi;
    s->music_offset_square2 = a.off_sq2; s->music_offset_square1 = a.off_sq1;
    s->music_offset_triangle = a.off_tri;
    s->pause_queue = g_PauseSoundQueue; s->area_music_queue = g_AreaMusicQueue;
    s->event_music_queue = g_EventMusicQueue; s->noise_queue = g_NoiseSoundQueue;
    s->square2_queue = g_Square2SoundQueue; s->square1_queue = g_Square1SoundQueue;
    s->music_offset_noise = a.off_noise; s->event_music_buffer = g_EventMusicBuffer;
    s->pause_buffer = a.pause_buf;
    s->square2_note_length_buffer = a.sq2_len_buf;
    s->square2_note_length_counter = a.sq2_len; s->square2_envelope = a.sq2_env;
    s->square1_note_length_counter = a.sq1_len; s->square1_envelope = a.sq1_env;
    s->triangle_note_length_buffer = a.tri_len_buf;
    s->triangle_note_length_counter = a.tri_len;
    s->noise_beat_length_counter = a.noise_len;
    s->square1_sfx_length_counter = a.sq1_sfx_len;
    s->square2_sfx_length_counter = a.sq2_sfx_len;
    s->sfx_secondary_counter = a.secondary;
    s->noise_sfx_length_counter = a.noise_sfx_len; s->dac_counter = a.dac_counter;
    s->noise_loopback_offset = a.noise_loop;
    s->note_length_table_adder = a.note_adder;
    s->area_music_buffer_alt = a.area_alt; s->pause_mode_flag = a.pause_mode;
    s->ground_music_header_offset = a.ground_header;
    s->alternate_register_content_flag = a.alt_reg;
}
