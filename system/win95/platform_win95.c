/* Native Win32 frontend for Windows 95/98-class hosts.
 *
 * This adapter deliberately uses only APIs present in Win95: GDI/DIBSection for
 * presentation, GetAsyncKeyState for input, and timeGetTime for pacing.  Audio
 * is implemented in the companion apu_win95.cpp with WinMM waveOut.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../platform.h"
#include "../common/nes_rgb.h"
#include "../common/ppu_memory.h"
#include "../sdl/video_soft.h"
#include "../../engine/assets.h"

static HINSTANCE instance_handle;
static HWND window_handle;
static HBITMAP dib_bitmap;
static HDC dib_dc;
static uint8_t *dib_pixels;
typedef struct {
    BITMAPINFOHEADER bmiHeader;
    RGBQUAD bmiColors[256];
} Win95BitmapInfo;

static Win95BitmapInfo dib_info;
static InputState current_input;
static uint8_t quit_requested;
static uint8_t configured_headless;
static uint8_t class_registered;
static uint8_t timer_resolution;
static int configured_scale;
static int configured_audio_rate;
static int configured_audio_hifi;
static int scale_factor = 2;
static DWORD next_frame_deadline;
static unsigned frame_fraction;
static uint8_t pacing_started;

#ifndef NDEBUG
static int frame_count;

static int save_frame_as_ppm(const char *filename)
{
    FILE *file;
    const uint8_t *indices;
    int x, y;

    file = fopen(filename, "wb");
    if (!file)
        return -1;
    fprintf(file, "P6\n%d %d\n255\n", VIDEO_WIDTH, VIDEO_HEIGHT);
    indices = video_indices();
    for (y = 0; y < VIDEO_HEIGHT; ++y) {
        for (x = 0; x < VIDEO_WIDTH; ++x) {
            const uint8_t *rgb = nes_rgb[indices[y * VIDEO_WIDTH + x] & 0x3F];
            fwrite(rgb, 1, 3, file);
        }
    }
    fclose(file);
    return 0;
}

void platform_save_frame(void)
{
    char filename[256];
    snprintf(filename, sizeof(filename), "test_output/frame_%04d.ppm",
             frame_count);
    if (save_frame_as_ppm(filename) == 0)
        ++frame_count;
}
#else
void platform_save_frame(void) {}
#endif

static void pump_messages(void)
{
    MSG message;
    while (PeekMessage(&message, NULL, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT)
            quit_requested = 1;
        TranslateMessage(&message);
        DispatchMessage(&message);
    }
}

static void paint_dib(HDC target)
{
    if (!dib_pixels)
        return;
    SetStretchBltMode(target, COLORONCOLOR);
    StretchDIBits(target, 0, 0, VIDEO_WIDTH * scale_factor,
                  VIDEO_HEIGHT * scale_factor, 0, 0, VIDEO_WIDTH,
                  VIDEO_HEIGHT, dib_pixels, (BITMAPINFO *)&dib_info,
                  DIB_RGB_COLORS,
                  SRCCOPY);
}

static LRESULT CALLBACK window_proc(HWND window, UINT message,
                                    WPARAM wparam, LPARAM lparam)
{
    PAINTSTRUCT paint;
    HDC target;

    (void)wparam;
    (void)lparam;
    switch (message) {
    case WM_CLOSE:
        quit_requested = 1;
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        window_handle = NULL;
        PostQuitMessage(0);
        return 0;
    case WM_PAINT:
        target = BeginPaint(window, &paint);
        paint_dib(target);
        EndPaint(window, &paint);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    default:
        return DefWindowProc(window, message, wparam, lparam);
    }
}

static int create_display(void)
{
    WNDCLASS window_class;
    RECT frame;
    DWORD style;
    int width = VIDEO_WIDTH * scale_factor;
    int height = VIDEO_HEIGHT * scale_factor;
    int i;

    instance_handle = GetModuleHandle(NULL);
    memset(&window_class, 0, sizeof(window_class));
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance_handle;
    window_class.hCursor = LoadCursor(NULL, IDC_ARROW);
    window_class.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    window_class.lpszClassName = "SMB2NativeWin95";
    if (!RegisterClass(&window_class))
        return -1;
    class_registered = 1;

    memset(&dib_info, 0, sizeof(dib_info));
    dib_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    dib_info.bmiHeader.biWidth = VIDEO_WIDTH;
    dib_info.bmiHeader.biHeight = -VIDEO_HEIGHT; /* top-down */
    dib_info.bmiHeader.biPlanes = 1;
    dib_info.bmiHeader.biBitCount = 8;
    dib_info.bmiHeader.biCompression = BI_RGB;
    dib_info.bmiHeader.biClrUsed = 256;
    dib_info.bmiHeader.biClrImportant = 64;
    for (i = 0; i < 64; ++i) {
        dib_info.bmiColors[i].rgbRed = nes_rgb[i][0];
        dib_info.bmiColors[i].rgbGreen = nes_rgb[i][1];
        dib_info.bmiColors[i].rgbBlue = nes_rgb[i][2];
    }
    dib_bitmap = CreateDIBSection(NULL, (BITMAPINFO *)&dib_info, DIB_RGB_COLORS,
                                  (void **)&dib_pixels, NULL, 0);
    if (!dib_bitmap)
        return -1;
    dib_dc = CreateCompatibleDC(NULL);
    if (!dib_dc)
        return -1;
    SelectObject(dib_dc, dib_bitmap);
    memset(dib_pixels, 0, VIDEO_WIDTH * VIDEO_HEIGHT);

    style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    frame.left = 0;
    frame.top = 0;
    frame.right = width;
    frame.bottom = height;
    AdjustWindowRect(&frame, style, FALSE);
    window_handle = CreateWindow(window_class.lpszClassName,
                                 "Super Mario Bros. (Win32)", style,
                                 CW_USEDEFAULT, CW_USEDEFAULT,
                                 frame.right - frame.left,
                                 frame.bottom - frame.top, NULL, NULL,
                                 instance_handle, NULL);
    if (!window_handle)
        return -1;
    ShowWindow(window_handle, SW_SHOWNORMAL);
    UpdateWindow(window_handle);
    return 0;
}

void platform_set_headless(uint8_t enabled)
{
    configured_headless = enabled ? 1 : 0;
}

void platform_set_options(const PlatformOptions *options)
{
    configured_scale = 0;
    configured_audio_rate = 0;
    configured_audio_hifi = 0;
    if (!options)
        return;
    if (options->scale >= 1 && options->scale <= 4)
        configured_scale = options->scale;
    if (options->audio_rate == 22050 || options->audio_rate == 44100 ||
        options->audio_rate == 48000)
        configured_audio_rate = options->audio_rate;
    configured_audio_hifi = options->audio_hifi ? 1 : 0;
}

int platform_audio_rate(void)
{
    return configured_audio_rate;
}

int platform_audio_hifi(void)
{
    return configured_audio_hifi;
}

uint8_t platform_win95_is_headless(void)
{
    return configured_headless;
}

void platform_init(void)
{
    quit_requested = 0;
    scale_factor = configured_scale ? configured_scale : 2;
    next_frame_deadline = 0;
    frame_fraction = 0;
    pacing_started = 0;

    if (!configured_headless) {
        if (create_display() != 0) {
            fprintf(stderr, "Win32 display initialization failed\n");
            quit_requested = 1;
            return;
        }
        if (timeBeginPeriod(1) == TIMERR_NOERROR)
            timer_resolution = 1;
    }

    PPU_Init();
    if (Assets_Init() != 0 || video_init() != 0) {
        quit_requested = 1;
        return;
    }
    printf("Native Win32 platform initialized (%dx%d)\n",
           VIDEO_WIDTH * scale_factor, VIDEO_HEIGHT * scale_factor);
}

void platform_shutdown(void)
{
    platform_audio_shutdown();
    video_shutdown();
    if (dib_dc)
        DeleteDC(dib_dc);
    if (dib_bitmap)
        DeleteObject(dib_bitmap);
    dib_dc = NULL;
    dib_bitmap = NULL;
    dib_pixels = NULL;
    if (window_handle)
        DestroyWindow(window_handle);
    pump_messages();
    if (class_registered) {
        UnregisterClass("SMB2NativeWin95", instance_handle);
        class_registered = 0;
    }
    if (timer_resolution) {
        timeEndPeriod(1);
        timer_resolution = 0;
    }
}

void platform_read_input(InputState *state)
{
    memset(&current_input, 0, sizeof(current_input));
    if (!configured_headless) {
        pump_messages();
        if ((GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0)
            quit_requested = 1;
        current_input.a = (GetAsyncKeyState('X') & 0x8000) != 0 ||
                          (GetAsyncKeyState(VK_NUMPAD0) & 0x8000) != 0;
        current_input.b = (GetAsyncKeyState('Z') & 0x8000) != 0 ||
                          (GetAsyncKeyState(VK_DECIMAL) & 0x8000) != 0;
        current_input.select = (GetAsyncKeyState(VK_BACK) & 0x8000) != 0;
        current_input.start = (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0;
        current_input.up = (GetAsyncKeyState(VK_UP) & 0x8000) != 0;
        current_input.down = (GetAsyncKeyState(VK_DOWN) & 0x8000) != 0;
        current_input.left = (GetAsyncKeyState(VK_LEFT) & 0x8000) != 0;
        current_input.right = (GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0;
    }
    *state = current_input;
}

uint8_t platform_should_quit(void)
{
    return quit_requested;
}

static void advance_frame_deadline(void)
{
    next_frame_deadline += 16;
    frame_fraction += 2;
    if (frame_fraction >= 3) {
        ++next_frame_deadline;
        frame_fraction -= 3;
    }
}

void platform_wait_frame(void)
{
    DWORD now;
    LONG remaining;

    if (configured_headless)
        return;
    pump_messages();
    now = timeGetTime();
    if (!pacing_started) {
        next_frame_deadline = now;
        frame_fraction = 0;
        pacing_started = 1;
    }
    advance_frame_deadline();
    for (;;) {
        now = timeGetTime();
        remaining = (LONG)(next_frame_deadline - now);
        if (remaining <= 0)
            break;
        Sleep(remaining > 2 ? (DWORD)(remaining - 1) : 1);
        pump_messages();
        if (quit_requested)
            break;
    }
    if ((LONG)(now - next_frame_deadline) > 4 * 17) {
        next_frame_deadline = now;
        frame_fraction = 0;
    }
}

void platform_render_begin(void)
{
    video_render_begin();
}

void platform_render_end(void)
{
    video_render_end();
    if (window_handle) {
        /* video_soft keeps the canonical palette-index image in its own
         * platform-neutral buffer.  The DIB is the native presentation copy. */
        memcpy(dib_pixels, video_indices(), VIDEO_WIDTH * VIDEO_HEIGHT);
        HDC target = GetDC(window_handle);
        if (target) {
            paint_dib(target);
            ReleaseDC(window_handle, target);
        }
        pump_messages();
    }
}

const uint8_t *platform_get_frame_indices(void)
{
    return video_indices();
}
