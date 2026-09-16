/* main.c - SDL entry point for the Super Mario Bros. reimplementation */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef DOS
#include <sys/time.h>
#endif
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#define os_close _close
#define os_dup _dup
#define os_dup2 _dup2
#define os_fdopen _fdopen
#define os_fileno _fileno
#else
#include <unistd.h>
#define os_close close
#define os_dup dup
#define os_dup2 dup2
#define os_fdopen fdopen
#define os_fileno fileno
#endif

#include "constants/defs.h"
#include "system/platform.h"
#ifdef DOS_BENCH
#include "system/dos/dos_bench.h"
#endif
#include "opermode.h"
#include "nmi.h"
#include "score.h"
#include "constants/globals.h"
#include "engine/assets.h"
#ifndef DOS
#include "system/fm2.h"
#include "system/state_stream.h"
#endif

#define FRAME_WIDTH 256
#define FRAME_HEIGHT 240
#define FRAME_PIXELS (FRAME_WIDTH * FRAME_HEIGHT)

#ifndef DOS
typedef struct {
    int headless;
    int save_frames;
    long max_frames;
    uint32_t tas_start;
    PlatformOptions platform;
    const char* tas_path;
    const char* frame_stream_path;
    const char* state_stream_path;
    const char* nmi_inputs_path;
} RunOptions;

static void print_usage(const char* program) {
    fprintf(stderr,
        "Usage: %s [options]\n"
        "  --headless                 Run without an SDL window or frame pacing\n"
        "  --tas PATH                 Replay a text FM2 v3 power-on movie\n"
        "  --tas-start FRAME          Start candidate input at this FM2 frame\n"
        "  --frames COUNT             Stop after COUNT candidate frames\n"
        "  --scale N                  Integer display scale (1..4)\n"
        "  --audio-rate HZ            Audio output rate (22050, 44100, or 48000)\n"
        "  --audio-hifi               Enable high-quality audio mixing when available\n"
        "  --dump-frame-stream PATH   Write SMBFRM1 palette-index stream ('-' = stdout)\n"
        "  --dump-state-stream PATH   Write SMBSTA2 canonical RAM state stream\n"
        "  --nmi-inputs PATH          Consume two controller bytes by actual NMI ordinal\n"
        "  --save-frames              Save legacy PPM files under test_output/\n"
        "  --help                     Show this help\n", program);
}

static int parse_nonnegative(const char* text, unsigned long* value) {
    char* end = NULL;
    unsigned long parsed;
    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (errno || end == text || *end != '\0') return -1;
    *value = parsed;
    return 0;
}

static int parse_options(int argc, char** argv, RunOptions* options) {
    int i;
    memset(options, 0, sizeof(*options));
    options->max_frames = -1;
#ifndef NDEBUG
    if (argc == 1) {
        options->headless = 1;
        options->save_frames = 1;
        options->max_frames = 1800;
    }
#endif

    for (i = 1; i < argc; i++) {
        unsigned long value;
        if (strcmp(argv[i], "--headless") == 0 || strcmp(argv[i], "headless") == 0) {
            options->headless = 1;
        } else if (strcmp(argv[i], "--save-frames") == 0) {
            options->save_frames = 1;
        } else if (strcmp(argv[i], "--tas") == 0 && i + 1 < argc) {
            options->tas_path = argv[++i];
            options->headless = 1;
        } else if (strcmp(argv[i], "--tas-start") == 0 && i + 1 < argc) {
            if (parse_nonnegative(argv[++i], &value) < 0) return -1;
            options->tas_start = (uint32_t)value;
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            if (parse_nonnegative(argv[++i], &value) < 0) return -1;
            options->max_frames = (long)value;
        } else if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc) {
            if (parse_nonnegative(argv[++i], &value) < 0 || value < 1 || value > 4) {
                fprintf(stderr, "--scale must be an integer from 1 to 4\n");
                return -1;
            }
            options->platform.scale = (int)value;
        } else if (strcmp(argv[i], "--audio-rate") == 0 && i + 1 < argc) {
            if (parse_nonnegative(argv[++i], &value) < 0 ||
                (value != 22050 && value != 44100 && value != 48000)) {
                fprintf(stderr, "--audio-rate must be 22050, 44100, or 48000 Hz\n");
                return -1;
            }
            options->platform.audio_rate = (int)value;
        } else if (strcmp(argv[i], "--audio-hifi") == 0) {
            options->platform.audio_hifi = 1;
        } else if (strcmp(argv[i], "--dump-frame-stream") == 0 && i + 1 < argc) {
            options->frame_stream_path = argv[++i];
            options->headless = 1;
        } else if (strcmp(argv[i], "--dump-state-stream") == 0 && i + 1 < argc) {
            options->state_stream_path = argv[++i];
            options->headless = 1;
        } else if (strcmp(argv[i], "--nmi-inputs") == 0 && i + 1 < argc) {
            options->nmi_inputs_path = argv[++i];
            options->headless = 1;
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            exit(0);
        } else {
            fprintf(stderr, "Unknown or incomplete option: %s\n", argv[i]);
            return -1;
        }
    }

    if (options->tas_start && !options->tas_path) {
        fprintf(stderr, "--tas-start requires --tas\n");
        return -1;
    }
    if (options->frame_stream_path && !options->tas_path && options->max_frames < 0) {
        fprintf(stderr, "--dump-frame-stream without --tas requires --frames\n");
        return -1;
    }
    if (options->state_stream_path && !options->tas_path && options->max_frames < 0) {
        fprintf(stderr, "--dump-state-stream without --tas requires --frames\n");
        return -1;
    }
    if (options->frame_stream_path && options->state_stream_path &&
        strcmp(options->frame_stream_path, "-") == 0 &&
        strcmp(options->state_stream_path, "-") == 0) {
        fprintf(stderr, "frame and state streams cannot both use stdout\n");
        return -1;
    }
#ifdef NDEBUG
    if (options->save_frames) {
        fprintf(stderr, "--save-frames is available only in debug builds\n");
        return -1;
    }
#endif
    return 0;
}

static FILE* open_frame_stream(const char* path) {
    FILE* stream;
    if (!path) return NULL;
    if (strcmp(path, "-") != 0) return fopen(path, "wb");

    fflush(stdout);
    {
        int stream_fd = os_dup(os_fileno(stdout));
        if (stream_fd < 0 || os_dup2(os_fileno(stderr), os_fileno(stdout)) < 0) {
            if (stream_fd >= 0) os_close(stream_fd);
            return NULL;
        }
        stream = os_fdopen(stream_fd, "wb");
    }
#ifdef _WIN32
    if (stream) _setmode(os_fileno(stream), _O_BINARY);
#endif
    if (stream) setvbuf(stream, NULL, _IONBF, 0);
    return stream;
}

static int write_stream_header(FILE* stream) {
    static const uint8_t header[16] = {
        'S','M','B','F','R','M','1','\0', 0x00,0x01, 0xF0,0x00, 1,0,0,0
    };
    return fwrite(header, 1, sizeof(header), stream) == sizeof(header) ? 0 : -1;
}

static int write_stream_frame(FILE* stream, uint32_t frame_number) {
    uint8_t number[4];
    const uint8_t* frame = platform_get_frame_indices();
    number[0] = (uint8_t)frame_number;
    number[1] = (uint8_t)(frame_number >> 8);
    number[2] = (uint8_t)(frame_number >> 16);
    number[3] = (uint8_t)(frame_number >> 24);
    if (!frame || fwrite(number, 1, 4, stream) != 4) return -1;
    return fwrite(frame, 1, FRAME_PIXELS, stream) == FRAME_PIXELS ? 0 : -1;
}
#endif

/* Execute one output frame. Models NES/FCEUX frame order:
 * FCEUX applies movie input record N at the START of emulated frame N+1
 * (verified against reference RAM traces: START at input frame 41 is seen
 * by the SMB menu NMI on output frame 42). The NMI's ReadJoypads loads the
 * latch into g_SavedJoypadBits before OperMode runs, then the frame renders
 * with the latch sampled at NMI start. Video frames 0-3 predate NMIs. */
#define NMI_FIRST_VIDEO_FRAME 5

static uint8_t pack_controller_input(const ControllerInput *input)
{
    return (uint8_t)((input->a << 7) | (input->b << 6) |
                     (input->select << 5) | (input->start << 4) |
                     (input->up << 3) | (input->down << 2) |
                     (input->left << 1) | input->right);
}

static void game_tick(int has_input_override, uint8_t input_override0,
                      uint8_t input_override1, uint32_t video_frame) {
    InputState input;
    uint8_t new_bits[2];

    if (has_input_override) {
        memset(&input, 0, sizeof(input));
    } else {
        platform_read_input(&input);
    }
    new_bits[0] = pack_controller_input(&input.controllers[0]);
    new_bits[1] = pack_controller_input(&input.controllers[1]);
    if (has_input_override) {
        new_bits[0] = input_override0;
        new_bits[1] = input_override1;
    }

    if (video_frame >= NMI_FIRST_VIDEO_FRAME) {
#ifdef DOS_BENCH
        uclock_t started = dos_bench_now();
#endif
        NMI_Tick(new_bits[0], new_bits[1]);
#ifdef DOS_BENCH
        g_DosBench.nmi_ticks += dos_bench_now() - started;
#endif
    }
#ifdef DOS_BENCH
    if (!g_DosBench.no_audio) {
        uclock_t started = dos_bench_now();
        platform_audio_frame();
        g_DosBench.audio_ticks += dos_bench_now() - started;
    }
#else
    platform_audio_frame();
#endif
}

static void game_render(void) {
    platform_render_begin();
    platform_render_end();
}

static void game_frame(int has_input_override, uint8_t input_override0,
                       uint8_t input_override1, uint32_t video_frame) {
    game_tick(has_input_override, input_override0, input_override1, video_frame);
    game_render();
}

static void initialize_game_state(void) {
    g_NumberOfPlayers = 0; /* NumberOfPlayers: 0=1P, 1=2P (main.asm) */
    g_CurrentPlayer = 0;
    g_WorldNumber = WORLD_1;
    g_LevelNumber = 0;
    g_NumberofLives = 3;
    g_OnscreenPlayerInfo[0] = 3;
    g_OffscreenPlayerInfo[0] = 3;
    g_CoinTally = 0;
    g_PlayerScore = 0;
    Score_Reset();

    OperMode_SetMode(TITLE_SCREEN_MODE);
    g_GameEngineSubroutine = 0;
    g_FrameCounter = 0;
}

static void game_loop_normal(void) {
    uint32_t output_frame = 0;
    printf("SMB2: Entering interactive game loop\n");
#ifdef DOS
    {
        /* Presentation can become slower while Mode X is restoring moving
         * sprites.  Keep the NES/NMI and PCM clock at the NTSC rate, and
         * render only the newest state when two ticks became due. */
        const uint64_t tick_threshold = (uint64_t)UCLOCKS_PER_SEC * 10000u;
        uclock_t last = uclock();
        uint64_t accumulator = tick_threshold;
        while (!platform_should_quit()) {
            uclock_t now;
            unsigned ticks = 0;
            platform_wait_frame();
            now = uclock();
            accumulator += (uint64_t)(now - last) * 600986u;
            last = now;
            if (accumulator > tick_threshold * 8u)
                accumulator = tick_threshold * 8u;
            while (accumulator >= tick_threshold && ticks < 8) {
                game_tick(0, 0, 0, output_frame++);
                accumulator -= tick_threshold;
                ticks++;
            }
            if (ticks)
                game_render();
        }
    }
#else
    while (!platform_should_quit()) {
        platform_wait_frame();
        game_frame(0, 0, 0, output_frame++);
    }
#endif
}

#ifndef DOS
static int game_loop_headless(const RunOptions* options, Fm2Movie* movie,
                              FILE* stream, FILE* state_stream,
                              FILE* nmi_inputs) {
    uint32_t output_frame = 0;
    uint32_t nmi_step = 0;
    int tas_active = movie && movie->file;
    SmbStateSnapshot state_snapshot;
    printf("SMB2: Entering headless mode\n");

#ifndef NDEBUG
    if (options->save_frames) {
#ifdef _WIN32
        system("if not exist test_output mkdir test_output");
#else
        system("mkdir -p test_output");
#endif
    }
#endif

    while (options->max_frames < 0 || (long)output_frame < options->max_frames) {
        uint8_t tas_buttons[2] = {0, 0};
        uint32_t tas_commands = 0;

        if (tas_active) {
            char error[256];
            int status = fm2_next(movie, tas_buttons, &tas_commands, error, sizeof(error));
            if (status == 0) break;
            if (status < 0) {
                fprintf(stderr, "FM2 error at input frame %u: %s\n", movie->frame, error);
                return 2;
            }
            if (tas_commands != 0) {
                uint32_t input_frame = movie->frame - 1;
                if (!((tas_commands == 1 || tas_commands == 2) &&
                      input_frame == 0 && options->tas_start == 0)) {
                    fprintf(stderr, "Unsupported FM2 command %u at input frame %u\n",
                            tas_commands, input_frame);
                    return 2;
                }
            }
        }

        if (output_frame >= NMI_FIRST_VIDEO_FRAME && g_NMIBusy == 0) {
            if (nmi_inputs) {
                int input0 = fgetc(nmi_inputs);
                int input1 = fgetc(nmi_inputs);
                if (input0 == EOF || input1 == EOF) {
                    if (feof(nmi_inputs)) break;
                    fprintf(stderr, "Failed to read NMI-indexed input\n");
                    return 2;
                }
                tas_buttons[0] = (uint8_t)input0;
                tas_buttons[1] = (uint8_t)input1;
            }
        }
        if (state_stream && output_frame >= NMI_FIRST_VIDEO_FRAME && g_NMIBusy == 0) {
            smb_state_snapshot(&state_snapshot);
            if (smb_state_write_record(state_stream, nmi_step++,
                                       SMB_STATE_FLAG_NMI_RAN,
                                       tas_buttons[0], tas_buttons[1],
                                       &state_snapshot) < 0) {
                fprintf(stderr, "Failed to write candidate state stream\n");
                return 2;
            }
        }
        game_frame(1, tas_buttons[0], tas_buttons[1], output_frame);
        if (stream && write_stream_frame(stream, output_frame) < 0) {
            fprintf(stderr, "Failed to write candidate frame stream\n");
            return 2;
        }
#ifndef NDEBUG
        if (options->save_frames) platform_save_frame();
#endif
        output_frame++;
    }

    printf("SMB2: Headless mode complete (%u frames rendered)\n", output_frame);
    return 0;
}
#endif

int main(int argc, char* argv[]) {
    Assets_SetArgv0(argv[0]);
#ifdef DOS
#ifdef DOS_BENCH
    dos_bench_parse(argc, argv);
#else
    (void)argc;
#endif
    printf("SMB2: Starting (DOS VGA)...\n");
    platform_set_headless(0);
    platform_init();
#ifdef DOS_BENCH
    if (!g_DosBench.no_audio)
        platform_audio_init();
#else
    platform_audio_init();
#endif

    initialize_game_state();

#ifdef DOS_BENCH
    dos_bench_start();
    {
        uint32_t output_frame;
        uint32_t logic_frame = 0;
        const uint64_t tick_threshold = (uint64_t)UCLOCKS_PER_SEC * 10000u;
        uint64_t accumulator = tick_threshold;
        uclock_t logic_last = uclock();
        for (output_frame = 0;
             output_frame < g_DosBench.frames && !platform_should_quit();
             ++output_frame) {
            uclock_t frame_started;
            uclock_t now;
            platform_wait_frame();
            now = uclock();
            accumulator += (uint64_t)(now - logic_last) * 600986u;
            logic_last = now;
            if (accumulator > tick_threshold * 8u)
                accumulator = tick_threshold * 8u;
            frame_started = dos_bench_now();
            if (g_DosBench.diagnostic) {
                platform_render_begin();
                platform_render_end();
            } else {
                unsigned ticks = 0;
                while (accumulator >= tick_threshold && ticks < 8) {
                    game_tick(0, 0, 0, logic_frame++);
                    accumulator -= tick_threshold;
                    ticks++;
                }
                g_DosBench.logic_ticks += ticks;
                if (ticks > g_DosBench.max_catchup_ticks)
                    g_DosBench.max_catchup_ticks = ticks;
                game_render();
            }
            dos_bench_record_frame(dos_bench_now() - frame_started);
        }
        dos_bench_write(output_frame);
    }
#else
    game_loop_normal();
#endif
    platform_shutdown();
    return 0;
#else
    RunOptions options;
    Fm2Movie movie;
    FILE* frame_stream = NULL;
    FILE* state_stream = NULL;
    FILE* nmi_inputs = NULL;
    int result = 0;
    char error[256];

    memset(&movie, 0, sizeof(movie));
    if (parse_options(argc, argv, &options) < 0) {
        print_usage(argv[0]);
        return 2;
    }
    if (options.frame_stream_path) {
        frame_stream = open_frame_stream(options.frame_stream_path);
        if (!frame_stream) {
            fprintf(stderr, "Cannot open frame stream '%s'\n", options.frame_stream_path);
            return 2;
        }
    }
    if (options.state_stream_path) {
        SmbStateSnapshot initial_state;
        state_stream = open_frame_stream(options.state_stream_path);
        if (!state_stream) {
            fprintf(stderr, "Failed to open state stream: %s\n", options.state_stream_path);
            result = 2;
            goto cleanup;
        }
        smb_state_snapshot(&initial_state);
        if (smb_state_write_header(state_stream, &initial_state) < 0) {
            fprintf(stderr, "Failed to write state stream header\n");
            result = 2;
            goto cleanup;
        }
    }
    if (options.nmi_inputs_path) {
        nmi_inputs = fopen(options.nmi_inputs_path, "rb");
        if (!nmi_inputs) {
            fprintf(stderr, "Cannot open NMI input stream '%s'\n",
                    options.nmi_inputs_path);
            result = 2;
            goto cleanup;
        }
    }

    printf("SMB2: Starting...\n");
    platform_set_options(&options.platform);
    platform_set_headless((uint8_t)options.headless);
    platform_init();
    platform_audio_init();

    initialize_game_state();

    if (options.tas_path) {
        if (fm2_open(&movie, options.tas_path, error, sizeof(error)) < 0) {
            fprintf(stderr, "FM2 error: %s\n", error);
            result = 2;
            goto cleanup;
        }
        if (fm2_skip(&movie, options.tas_start, error, sizeof(error)) < 0) {
            fprintf(stderr, "FM2 error: %s\n", error);
            result = 2;
            goto cleanup;
        }
    }

    if (frame_stream && write_stream_header(frame_stream) < 0) {
        fprintf(stderr, "Failed to write candidate frame stream header\n");
        result = 2;
        goto cleanup;
    }

    if (options.headless) {
        result = game_loop_headless(&options, &movie, frame_stream, state_stream,
                                    nmi_inputs);
    } else {
        game_loop_normal();
    }

cleanup:
    fm2_close(&movie);
    if (frame_stream) fclose(frame_stream);
    if (state_stream) fclose(state_stream);
    if (nmi_inputs) fclose(nmi_inputs);
    platform_shutdown();
    return result;
#endif
}
