/* ANSI terminal host: NTSC-rate game logic, 30 Hz text presentation, no audio. */
#define _POSIX_C_SOURCE 200809L

#include "../platform.h"
#include "../common/nes_rgb.h"
#include "../common/ppu_memory.h"
#include "../sdl/video_soft.h"
#include "../../engine/assets.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define MAX_COLS 256
#define MAX_ROWS 112
#define OUT_BYTES (512 * 1024)

typedef struct {
    uint8_t fg, bg;
} TermCell;

static struct termios saved_termios;
static int saved_flags;
static int terminal_active;
static volatile sig_atomic_t quit_requested;
static unsigned present_phase;
static struct timespec next_tick;
static TermCell previous[MAX_COLS * MAX_ROWS];
static int previous_valid;
static int previous_cols, previous_rows;
static unsigned char direction[4], pulse[2], jump_frames, run_held;
static unsigned char escape_state;
static char output[OUT_BYTES];
static int dump_frame, dump_presentations;

static void write_all(const char *data, size_t length)
{
    while (length) {
        ssize_t wrote = write(STDOUT_FILENO, data, length);
        if (wrote > 0) {
            data += wrote;
            length -= (size_t)wrote;
        } else if (wrote < 0 && errno == EINTR) {
            continue;
        } else if (wrote < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            struct pollfd fd;
            fd.fd = STDOUT_FILENO;
            fd.events = POLLOUT;
            fd.revents = 0;
            if (poll(&fd, 1, 1000) >= 0 || errno == EINTR)
                continue;
            quit_requested = 1;
            break;
        } else {
            quit_requested = 1;
            break;
        }
    }
}

static void restore_terminal(void)
{
    if (!terminal_active)
        return;
    {
        static const char leave[] = "\033[0m\033[?25h\033[?1049l";
        write_all(leave, sizeof(leave) - 1);
    }
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_termios);
    fcntl(STDIN_FILENO, F_SETFL, saved_flags);
    terminal_active = 0;
}

static void on_signal(int signal_number)
{
    (void)signal_number;
    quit_requested = 1;
}

static void add_ns(struct timespec *time, long ns)
{
    time->tv_nsec += ns;
    while (time->tv_nsec >= 1000000000L) {
        time->tv_nsec -= 1000000000L;
        time->tv_sec++;
    }
}

static int compare_time(const struct timespec *a, const struct timespec *b)
{
    if (a->tv_sec != b->tv_sec)
        return a->tv_sec < b->tv_sec ? -1 : 1;
    return (a->tv_nsec > b->tv_nsec) - (a->tv_nsec < b->tv_nsec);
}

static uint8_t representative_color(const uint8_t *frame, int x0, int y0,
                                    int width, int height)
{
    uint8_t colors[32], counts[32];
    int used = 0, best = 0, second = -1, x, y, i;
    memset(counts, 0, sizeof(counts));
    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++) {
            uint8_t color = frame[(y0 + y) * VIDEO_WIDTH + x0 + x] & 0x3f;
            for (i = 0; i < used && colors[i] != color; i++)
                ;
            if (i == used && used < (int)sizeof(colors)) {
                colors[used] = color;
                counts[used++] = 0;
            }
            if (i < used && ++counts[i] > counts[best])
                best = i;
        }
    for (i = 0; i < used; i++)
        if (i != best && (second < 0 || counts[i] > counts[second]))
            second = i;
    /* At 64x30 a thin glyph can occupy only a quarter of a 4x4 half-cell.
     * Preserve that coherent secondary color instead of averaging it away. */
    if (width >= 4 && second >= 0 && counts[best] >= 12 && counts[second] >= 3)
        best = second;
    return used ? colors[best] : 0;
}

static size_t append_text(size_t at, const char *text, size_t length)
{
    if (at + length > sizeof(output))
        return at;
    memcpy(output + at, text, length);
    return at + length;
}

static size_t append_format(size_t at, const char *format, int a, int b, int c)
{
    int wrote;
    if (at >= sizeof(output))
        return at;
    wrote = snprintf(output + at, sizeof(output) - at, format, a, b, c);
    if (wrote < 0 || (size_t)wrote >= sizeof(output) - at)
        return at;
    return at + (size_t)wrote;
}

static void terminal_present(void)
{
    struct winsize window;
    TermCell current[MAX_COLS * MAX_ROWS];
    const uint8_t *frame = video_indices();
    int cols, rows, sx, sy, half, row, col;
    int active_fg = -1, active_bg = -1;
    char status[160];
    size_t status_length = 0;
    size_t at = 0;

    memset(&window, 0, sizeof(window));
    if (!dump_frame)
        ioctl(STDOUT_FILENO, TIOCGWINSZ, &window);
    if (!dump_frame && window.ws_col >= 256 && window.ws_row >= 113) {
        cols = 256;
        rows = 112;
    } else if (dump_frame || (window.ws_col >= 128 && window.ws_row >= 61)) {
        cols = 128;
        rows = 60;
    } else {
        cols = 64;
        rows = 30;
    }
    if (cols != previous_cols || rows != previous_rows)
        previous_valid = 0;
    sx = VIDEO_WIDTH / cols;
    sy = VIDEO_HEIGHT / rows;
    half = sy / 2;
    for (row = 0; row < rows; row++)
        for (col = 0; col < cols; col++) {
            TermCell *cell = &current[row * cols + col];
            cell->fg = representative_color(frame, col * sx, row * sy, sx, half);
            cell->bg = representative_color(frame, col * sx, row * sy + half, sx,
                                            sy - half);
        }

    if (dump_frame && ++dump_presentations >= dump_frame) {
        FILE *source = fopen("terminal-source.ppm", "wb");
        FILE *preview = fopen("terminal-preview.ppm", "wb");
        int x, y;
        if (source) {
            fprintf(source, "P6\n%d %d\n255\n", VIDEO_WIDTH, VIDEO_HEIGHT);
            for (y = 0; y < VIDEO_HEIGHT; y++)
                for (x = 0; x < VIDEO_WIDTH; x++)
                    fwrite(nes_rgb[frame[y * VIDEO_WIDTH + x] & 0x3f], 1, 3, source);
            fclose(source);
        }
        if (preview) {
            fprintf(preview, "P6\n%d %d\n255\n", VIDEO_WIDTH, VIDEO_HEIGHT);
            for (y = 0; y < VIDEO_HEIGHT; y++)
                for (x = 0; x < VIDEO_WIDTH; x++) {
                    TermCell cell = current[(y / sy) * cols + x / sx];
                    uint8_t color = (y % sy) < half ? cell.fg : cell.bg;
                    fwrite(nes_rgb[color], 1, 3, preview);
                }
            fclose(preview);
        }
        quit_requested = 1;
        return;
    }
    if (dump_frame)
        return;

    if (!previous_valid)
        at = append_text(at, "\033[2J", 4);
    for (row = 0; row < rows; row++) {
        col = 0;
        while (col < cols) {
            int first, last;
            while (col < cols && previous_valid &&
                   current[row * cols + col].fg == previous[row * cols + col].fg &&
                   current[row * cols + col].bg == previous[row * cols + col].bg)
                col++;
            if (col == cols)
                break;
            first = col;
            last = col;
            while (++col < cols) {
                if (!previous_valid ||
                    current[row * cols + col].fg != previous[row * cols + col].fg ||
                    current[row * cols + col].bg != previous[row * cols + col].bg)
                    last = col;
                else if (col - last > 2)
                    break;
            }
            at = append_format(at, "\033[%d;%dH", row + 1, first + 1, 0);
            for (col = first; col <= last; col++) {
                TermCell cell = current[row * cols + col];
                const uint8_t *fg = nes_rgb[cell.fg];
                const uint8_t *bg = nes_rgb[cell.bg];
                if (cell.fg != active_fg) {
                    at = append_format(at, "\033[38;2;%d;%d;%dm", fg[0], fg[1], fg[2]);
                    active_fg = cell.fg;
                }
                if (cell.bg != active_bg) {
                    at = append_format(at, "\033[48;2;%d;%d;%dm", bg[0], bg[1], bg[2]);
                    active_bg = cell.bg;
                }
                at = append_text(at, "\xE2\x96\x80", 3); /* U+2580 upper half block */
            }
        }
    }
    at = append_text(at, "\033[0m", 4);
    status_length += (size_t)snprintf(status + status_length,
                                     sizeof(status) - status_length, "INPUT:");
#define ADD_INPUT(active, label)                                                \
    do {                                                                        \
        if (active)                                                             \
            status_length += (size_t)snprintf(status + status_length,           \
                sizeof(status) - status_length, " %s", label);                 \
    } while (0)
    ADD_INPUT(direction[0], "UP");
    ADD_INPUT(direction[1], "DOWN");
    ADD_INPUT(direction[2], "LEFT");
    ADD_INPUT(direction[3], "RIGHT");
    ADD_INPUT(jump_frames, "A(JUMP)");
    ADD_INPUT(run_held, "B(RUN)");
    ADD_INPUT(pulse[0], "START");
    ADD_INPUT(pulse[1], "SELECT");
#undef ADD_INPUT
    if (status_length == 6)
        status_length += (size_t)snprintf(status + status_length,
                                         sizeof(status) - status_length, " NONE");
    at = append_format(at, "\033[%d;%dH", rows + 1, 1, 0);
    at = append_text(at, "\033[2K\033[7m", 8);
    at = append_text(at, status, status_length);
    at = append_text(at, "\033[0m", 4);
    if (at)
        write_all(output, at);
    memcpy(previous, current, (size_t)(cols * rows) * sizeof(TermCell));
    previous_cols = cols;
    previous_rows = rows;
    previous_valid = 1;
}

void platform_set_headless(uint8_t enabled) { (void)enabled; }
void platform_set_options(const PlatformOptions *options) { (void)options; }
int platform_audio_rate(void) { return 0; }
int platform_audio_hifi(void) { return 0; }

void platform_init(void)
{
    struct termios raw;
    const char *dump = getenv("SMB_TERM_DUMP_FRAME");
    quit_requested = 0;
    dump_frame = dump ? atoi(dump) : 0;
    if (!dump_frame && (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO))) {
        fprintf(stderr, "smb2-terminal requires an interactive UTF-8 terminal\n");
        quit_requested = 1;
        return;
    }
    if (!dump_frame) {
        tcgetattr(STDIN_FILENO, &saved_termios);
        raw = saved_termios;
        raw.c_lflag &= (tcflag_t)~(ICANON | ECHO | ISIG);
        raw.c_iflag &= (tcflag_t)~(IXON | ICRNL);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        saved_flags = fcntl(STDIN_FILENO, F_GETFL, 0);
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
        fcntl(STDIN_FILENO, F_SETFL, saved_flags | O_NONBLOCK);
        terminal_active = 1;
        atexit(restore_terminal);
        signal(SIGINT, on_signal);
        signal(SIGTERM, on_signal);
        {
            static const char enter[] = "\033[?1049h\033[?25l";
            write_all(enter, sizeof(enter) - 1);
        }
    }
    PPU_Init();
    if (Assets_Init() != 0 || video_init() != 0) {
        quit_requested = 1;
        return;
    }
    clock_gettime(CLOCK_MONOTONIC, &next_tick);
}

void platform_shutdown(void)
{
    video_shutdown();
    restore_terminal();
}

void platform_read_input(InputState *state)
{
    unsigned char input[128];
    ssize_t count;
    int i;
    for (i = 0; i < 2; i++)
        if (pulse[i])
            pulse[i]--;
    if (jump_frames)
        jump_frames--;
    if (dump_frame) {
        memset(state, 0, sizeof(*state));
        return;
    }
    while ((count = read(STDIN_FILENO, input, sizeof(input))) > 0) {
        ssize_t p;
        for (p = 0; p < count; p++) {
            unsigned char c = input[p];
            if (escape_state == 1) {
                escape_state = c == '[' ? 2 : 0;
                if (escape_state)
                    continue;
            } else if (escape_state == 2) {
                switch (c) {
                case 'A': direction[0] ^= 1; direction[1] = 0; break;
                case 'B': direction[1] ^= 1; direction[0] = 0; break;
                case 'D': direction[2] ^= 1; direction[3] = 0; break;
                case 'C': direction[3] ^= 1; direction[2] = 0; break;
                }
                escape_state = 0;
                continue;
            }
            if (c == 0x1b) {
                escape_state = 1;
            } else if (c == 3 || c == 'q' || c == 'Q') {
                quit_requested = 1;
            } else if (c == 'x' || c == 'X') {
                /* Terminals do not report key-up.  Make jump a complete,
                 * predictable action instead of requiring a second press. */
                jump_frames = 24;
            } else if (c == 'c' || c == 'C') {
                jump_frames = 5;
            } else if (c == 'z' || c == 'Z') {
                run_held ^= 1;
            } else if (c == '\r' || c == '\n') {
                pulse[0] = 3;
            } else if (c == 0x7f || c == 0x08) {
                pulse[1] = 3;
            } else if (c == ' ') {
                memset(direction, 0, sizeof(direction));
            }
        }
    }
    memset(state, 0, sizeof(*state));
    state->up = direction[0];
    state->down = direction[1];
    state->left = direction[2];
    state->right = direction[3];
    state->a = jump_frames != 0;
    state->b = run_held;
    state->start = pulse[0] != 0;
    state->select = pulse[1] != 0;
}

uint8_t platform_should_quit(void) { return quit_requested ? 1 : 0; }

void platform_wait_frame(void)
{
    struct timespec now, delay;
    add_ns(&next_tick, 16639267L); /* 29780.5 / 1789773 seconds */
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (compare_time(&now, &next_tick) < 0) {
        delay.tv_sec = next_tick.tv_sec - now.tv_sec;
        delay.tv_nsec = next_tick.tv_nsec - now.tv_nsec;
        if (delay.tv_nsec < 0) {
            delay.tv_nsec += 1000000000L;
            delay.tv_sec--;
        }
        while (nanosleep(&delay, &delay) != 0 && errno == EINTR)
            ;
    } else if (now.tv_sec - next_tick.tv_sec > 1) {
        next_tick = now;
    }
}

void platform_render_begin(void) { video_render_begin(); }
void platform_render_end(void)
{
    video_render_end();
    if ((present_phase++ & 1u) == 0)
        terminal_present();
}
const uint8_t *platform_get_frame_indices(void) { return video_indices(); }
void platform_save_frame(void) {}
void platform_audio_init(void) {}
void platform_audio_shutdown(void) {}
void platform_apu_write(uint16_t address, uint8_t value)
{
    (void)address;
    (void)value;
}
void platform_audio_frame(void) {}
void platform_audio_trace_begin(uint32_t ordinal, const uint8_t *state,
                                uint16_t size)
{
    (void)ordinal;
    (void)state;
    (void)size;
}
void platform_audio_trace_end(const uint8_t *state, uint16_t size)
{
    (void)state;
    (void)size;
}
