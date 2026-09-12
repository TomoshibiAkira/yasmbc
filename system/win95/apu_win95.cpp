/* Native WinMM waveOut audio adapter for Windows 95/98-class hosts. */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "../../third_party/nes_snd_emu/nes_apu/Nes_Apu.h"
#include "../../third_party/nes_snd_emu/nes_apu/Multi_Buffer.h"
#include "../../third_party/nes_snd_emu/nes_apu/Nonlinear_Buffer.h"

namespace {
extern "C" uint8_t platform_win95_is_headless(void);

static const long kDefaultSampleRate = 22050;
static const long kHighSampleRate = 44100;
static const long kCpuClock = 1789773;
static const int kMaxSamples = 1024;
static const unsigned kRingSamples = 32768;
static const unsigned kRingMask = kRingSamples - 1;
static const unsigned kBufferCount = 4;
static const unsigned kBufferSamples = 1024;
static const unsigned kPrebufferSamples = kBufferCount * kBufferSamples;

Nes_Apu apu;
Mono_Buffer linear_buffer;
Nonlinear_Buffer nonlinear_buffer;
Multi_Buffer *buffer = &linear_buffer;
bool hifi_audio = false;
bool audio_device = false;
bool audio_started = false;
bool initialized = false;
blip_time_t write_time = 0;
uint32_t audio_frame = 0;
long output_sample_rate = kDefaultSampleRate;

blip_sample_t audio_ring[kRingSamples];
uint32_t ring_read = 0;
uint32_t ring_write = 0;
uint32_t audio_underruns = 0;
uint32_t audio_overruns = 0;

HWAVEOUT wave_handle = NULL;
WAVEHDR wave_headers[kBufferCount];
int16_t wave_storage[kBufferCount][kBufferSamples];
volatile uint8_t buffer_done[kBufferCount];

int flat_dmc_reader(void *, cpu_addr_t) { return 0x55; }

static long select_sample_rate(void)
{
    const char *setting = std::getenv("SMB_WIN95_AUDIO_RATE");
    char *end = 0;
    long parsed;

    if (!setting || !*setting)
        return kDefaultSampleRate;
    parsed = std::strtol(setting, &end, 10);
    if (*end == '\0' &&
        (parsed == kDefaultSampleRate || parsed == kHighSampleRate))
        return parsed;
    std::fprintf(stderr,
                 "APU: SMB_WIN95_AUDIO_RATE must be 22050 or 44100; "
                 "using %ld\n", kDefaultSampleRate);
    return kDefaultSampleRate;
}

void CALLBACK wave_callback(HWAVEOUT, UINT message, DWORD_PTR,
                            DWORD_PTR param1, DWORD_PTR)
{
    unsigned i;
    if (message != WOM_DONE)
        return;
    for (i = 0; i < kBufferCount; ++i) {
        if (param1 == (DWORD_PTR)&wave_headers[i]) {
            buffer_done[i] = 1;
            break;
        }
    }
}

static uint32_t ring_available(void)
{
    return ring_write - ring_read;
}

static void queue_samples(const blip_sample_t *samples, long count)
{
    uint32_t sample_count;
    uint32_t offset;
    uint32_t first;

    if (!audio_device || count <= 0)
        return;
    sample_count = (uint32_t)count;
    if (ring_available() + sample_count > kRingSamples) {
        uint32_t dropped = ring_available() + sample_count - kRingSamples;
        ring_read += dropped;
        audio_overruns += dropped;
    }
    offset = ring_write & kRingMask;
    first = kRingSamples - offset;
    if (first > sample_count)
        first = sample_count;
    std::memcpy(audio_ring + offset, samples,
                (size_t)first * sizeof(audio_ring[0]));
    if (first < sample_count)
        std::memcpy(audio_ring, samples + first,
                    (size_t)(sample_count - first) * sizeof(audio_ring[0]));
    ring_write += sample_count;
}

static void fill_block(unsigned index)
{
    uint32_t available = ring_available();
    uint32_t count = available < kBufferSamples ? available : kBufferSamples;
    uint32_t offset = ring_read & kRingMask;
    uint32_t first = kRingSamples - offset;

    if (first > count)
        first = count;
    if (first)
        std::memcpy(wave_storage[index], audio_ring + offset,
                    (size_t)first * sizeof(audio_ring[0]));
    if (first < count)
        std::memcpy(wave_storage[index] + first, audio_ring,
                    (size_t)(count - first) * sizeof(audio_ring[0]));
    ring_read += count;
    if (count < kBufferSamples) {
        std::memset(wave_storage[index] + count, 0,
                    (size_t)(kBufferSamples - count) * sizeof(int16_t));
        ++audio_underruns;
    }
}

static void submit_block(unsigned index)
{
    MMRESULT result;
    wave_headers[index].dwBufferLength = sizeof(wave_storage[index]);
    wave_headers[index].dwFlags = 0;
    buffer_done[index] = 0;
    result = waveOutPrepareHeader(wave_handle, &wave_headers[index],
                                  sizeof(wave_headers[index]));
    if (result != MMSYSERR_NOERROR) {
        buffer_done[index] = 1;
        return;
    }
    result = waveOutWrite(wave_handle, &wave_headers[index],
                          sizeof(wave_headers[index]));
    if (result != MMSYSERR_NOERROR) {
        waveOutUnprepareHeader(wave_handle, &wave_headers[index],
                               sizeof(wave_headers[index]));
        buffer_done[index] = 1;
    }
}

static void service_waveout(void)
{
    unsigned i;

    if (!audio_device)
        return;
    if (!audio_started) {
        if (ring_available() < kPrebufferSamples)
            return;
        for (i = 0; i < kBufferCount; ++i) {
            fill_block(i);
            submit_block(i);
        }
        audio_started = true;
        return;
    }
    for (i = 0; i < kBufferCount; ++i) {
        if (buffer_done[i]) {
            waveOutUnprepareHeader(wave_handle, &wave_headers[i],
                                   sizeof(wave_headers[i]));
            fill_block(i);
            submit_block(i);
        }
    }
}
}

extern "C" void platform_audio_init(void)
{
    WAVEFORMATEX format;
    const char *hifi_setting;
    unsigned i;

    if (initialized)
        return;
    output_sample_rate = select_sample_rate();
    hifi_setting = std::getenv("SMB_WIN95_AUDIO_HIFI");
    hifi_audio = hifi_setting && *hifi_setting &&
                 std::strcmp(hifi_setting, "0") != 0;
    buffer = hifi_audio ? static_cast<Multi_Buffer *>(&nonlinear_buffer)
                        : static_cast<Multi_Buffer *>(&linear_buffer);
    buffer->clock_rate(kCpuClock);
    if (buffer->sample_rate(output_sample_rate, 250)) {
        std::fprintf(stderr, "APU: failed to configure sample buffer\n");
        return;
    }
    buffer->clear();
    if (hifi_audio)
        nonlinear_buffer.enable_nonlinearity(apu, true);
    else {
        apu.volume(1.0);
        apu.output(linear_buffer.center());
    }
    buffer->bass_freq(16);
    apu.dmc_reader(flat_dmc_reader, 0);
    apu.reset(false);
    audio_frame = 0;
    write_time = 0;
    ring_read = ring_write = 0;
    audio_underruns = audio_overruns = 0;
    audio_started = false;
    wave_handle = NULL;
    std::memset((void *)buffer_done, 1, sizeof(buffer_done));

    /* Headless runs retain deterministic APU state but do not open hardware. */
    if (!platform_win95_is_headless()) {
        std::memset(&format, 0, sizeof(format));
        format.wFormatTag = WAVE_FORMAT_PCM;
        format.nChannels = 1;
        format.nSamplesPerSec = output_sample_rate;
        format.wBitsPerSample = 16;
        format.nBlockAlign = 2;
        format.nAvgBytesPerSec = output_sample_rate * 2;
        if (waveOutOpen(&wave_handle, WAVE_MAPPER, &format,
                        (DWORD_PTR)wave_callback, 0, CALLBACK_FUNCTION) !=
            MMSYSERR_NOERROR) {
            wave_handle = NULL;
            std::fprintf(stderr, "APU: waveOutOpen failed\n");
        } else {
            for (i = 0; i < kBufferCount; ++i) {
                std::memset(&wave_headers[i], 0, sizeof(wave_headers[i]));
                wave_headers[i].lpData = (LPSTR)wave_storage[i];
                wave_headers[i].dwUser = i;
            }
            audio_device = true;
        }
    }
    initialized = true;
}

extern "C" void platform_audio_shutdown(void)
{
    unsigned i;
    if (wave_handle) {
        waveOutReset(wave_handle);
        for (i = 0; i < kBufferCount; ++i) {
            if (wave_headers[i].dwFlags & WHDR_PREPARED)
                waveOutUnprepareHeader(wave_handle, &wave_headers[i],
                                       sizeof(wave_headers[i]));
        }
        waveOutClose(wave_handle);
    }
    wave_handle = NULL;
    audio_device = false;
    audio_started = false;
    initialized = false;
}

extern "C" void platform_apu_write(uint16_t address, uint8_t value)
{
    if (!initialized)
        return;
    write_time += 4;
    apu.write_register(write_time, address, value);
}

extern "C" void platform_audio_frame(void)
{
    blip_sample_t samples[kMaxSamples];
    long available;

    if (!initialized)
        return;
    {
        const blip_time_t clocks = (audio_frame & 1) ? 29781 : 29780;
        apu.end_frame(clocks);
        buffer->end_frame(clocks);
    }
    write_time = 0;
    ++audio_frame;
    available = buffer->samples_avail();
    while (available > 0) {
        long request = available > kMaxSamples ? kMaxSamples : available;
        long count = buffer->read_samples(samples, request);
        if (count <= 0)
            break;
        queue_samples(samples, count);
        available -= count;
    }
    service_waveout();
}

extern "C" void platform_audio_trace_begin(uint32_t, const uint8_t *, uint16_t) {}
extern "C" void platform_audio_trace_end(const uint8_t *, uint16_t) {}
