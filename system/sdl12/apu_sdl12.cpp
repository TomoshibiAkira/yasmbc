/* SDL 1.2 host adapter for the LGPL Nes_Snd_Emu 2A03 core. */

#include <SDL/SDL.h>

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "../../third_party/nes_snd_emu/nes_apu/Nes_Apu.h"
#include "../../third_party/nes_snd_emu/nes_apu/Multi_Buffer.h"
#include "../../third_party/nes_snd_emu/nes_apu/Nonlinear_Buffer.h"

namespace {
constexpr long kSampleRate = 22050;
constexpr long kCpuClock = 1789773;
constexpr int kMaxSamples = 1024;
constexpr unsigned kRingSamples = 32768;
constexpr unsigned kRingMask = kRingSamples - 1;
constexpr unsigned kPrebufferSamples = kSampleRate / 10;

Nes_Apu apu;
Mono_Buffer linear_buffer;
Nonlinear_Buffer nonlinear_buffer;
Multi_Buffer *buffer = &linear_buffer;
bool hifi_audio = false;
bool audio_device = false;
bool audio_started = false;
bool initialized = false;
bool audio_subsystem_started = false;
blip_time_t write_time = 0;
uint32_t audio_frame = 0;

/* The game thread is the sole producer and SDL's callback is the sole
 * consumer.  SDL_LockAudio protects the producer-side update while the
 * callback is running; monotonically increasing counters make wraparound
 * arithmetic and the full-buffer check inexpensive on 32-bit hosts. */
blip_sample_t audio_ring[kRingSamples];
volatile uint32_t ring_read = 0;
volatile uint32_t ring_write = 0;
uint32_t audio_underruns = 0;
uint32_t audio_overruns = 0;

int flat_dmc_reader(void *, cpu_addr_t) { return 0x55; }

void SDLCALL audio_callback(void *, Uint8 *stream, int length)
{
    const int sample_count = length / (int)sizeof(blip_sample_t);
    blip_sample_t *output = reinterpret_cast<blip_sample_t *>(stream);
    uint32_t read = ring_read;
    const uint32_t write = ring_write;
    uint32_t available = write - read;
    uint32_t produced = available < (uint32_t)sample_count
                            ? available : (uint32_t)sample_count;
    uint32_t offset = read & kRingMask;
    uint32_t first = kRingSamples - offset;

    if (first > produced)
        first = produced;
    if (first)
        std::memcpy(output, audio_ring + offset,
                    (size_t)first * sizeof(audio_ring[0]));
    if (first < produced)
        std::memcpy(output + first, audio_ring,
                    (size_t)(produced - first) * sizeof(audio_ring[0]));
    read += produced;
    if (produced < (uint32_t)sample_count) {
        std::memset(output + produced, 0,
                    (size_t)(sample_count - produced) * sizeof(*output));
        ++audio_underruns;
    }
    if (length & (int)(sizeof(blip_sample_t) - 1))
        stream[length - 1] = 0;
    ring_read = read;
}

void queue_samples(const blip_sample_t *samples, long count)
{
    uint32_t write;
    uint32_t read;
    uint32_t sample_count;
    uint32_t offset;
    uint32_t first;

    if (!audio_device || count <= 0)
        return;
    sample_count = (uint32_t)count;
    SDL_LockAudio();
    write = ring_write;
    read = ring_read;
    if (write - read + sample_count > kRingSamples) {
        uint32_t dropped = write - read + sample_count - kRingSamples;
        read += dropped;
        ring_read = read;
        audio_overruns += dropped;
    }
    offset = write & kRingMask;
    first = kRingSamples - offset;
    if (first > sample_count)
        first = sample_count;
    std::memcpy(audio_ring + offset, samples,
                (size_t)first * sizeof(audio_ring[0]));
    if (first < sample_count)
        std::memcpy(audio_ring, samples + first,
                    (size_t)(sample_count - first) * sizeof(audio_ring[0]));
    ring_write = write + sample_count;
    SDL_UnlockAudio();
}
}

extern "C" void platform_audio_init(void)
{
    SDL_AudioSpec wanted;
    const char *hifi_setting;

    if (initialized)
        return;

    /* The verifier's audio contract is register/state exact; PCM waveform
     * fidelity is deliberately not part of it.  A single linear buffer is
     * substantially cheaper on a Pentium II than the nonlinear pulse/TND
     * mixer.  Keep the latter available for listening tests and regression
     * comparisons without making it the slow-host default. */
    hifi_setting = std::getenv("SMB_SDL12_AUDIO_HIFI");
    hifi_audio = hifi_setting && *hifi_setting &&
                 std::strcmp(hifi_setting, "0") != 0;
    buffer = hifi_audio ? static_cast<Multi_Buffer *>(&nonlinear_buffer)
                        : static_cast<Multi_Buffer *>(&linear_buffer);
    buffer->clock_rate(kCpuClock);
    if (buffer->sample_rate(kSampleRate, 250)) {
        std::fprintf(stderr, "APU: failed to configure sample buffer\n");
        return;
    }
    buffer->clear();
    if (hifi_audio) {
        nonlinear_buffer.enable_nonlinearity(apu, true);
    } else {
        apu.volume(1.0);
        apu.output(linear_buffer.center());
    }
    buffer->bass_freq(16);
    apu.dmc_reader(flat_dmc_reader, nullptr);
    apu.reset(false);
    audio_frame = 0;
    write_time = 0;
    ring_read = ring_write = 0;
    audio_underruns = audio_overruns = 0;

    /* Headless verification deliberately does not initialize SDL.  The APU
     * still advances there, but no host callback is opened. */
    if (SDL_WasInit(SDL_INIT_VIDEO)) {
        if (!SDL_WasInit(SDL_INIT_AUDIO)) {
            if (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0)
                audio_subsystem_started = true;
            else
                std::fprintf(stderr, "APU: SDL audio unavailable: %s\n",
                             SDL_GetError());
        }
        if (SDL_WasInit(SDL_INIT_AUDIO)) {
            std::memset(&wanted, 0, sizeof(wanted));
            wanted.freq = kSampleRate;
            wanted.format = AUDIO_S16SYS;
            wanted.channels = 1;
            /* A larger callback block is kinder to a 233 MHz Pentium II and
             * the ring provides the remaining latency protection. */
            wanted.samples = 1024;
            wanted.callback = audio_callback;
            SDL_AudioSpec obtained;
            std::memset(&obtained, 0, sizeof(obtained));
            if (SDL_OpenAudio(&wanted, &obtained) != 0) {
                std::fprintf(stderr, "APU: SDL_OpenAudio failed: %s\n",
                             SDL_GetError());
            } else if (obtained.freq != kSampleRate ||
                       obtained.format != AUDIO_S16SYS ||
                       obtained.channels != 1) {
                /* The producer is deliberately format-free and the ring is
                 * sized in native samples.  Do not silently play at a
                 * converted rate: that turns a slow device into pitch drift
                 * and makes underruns look like game timing bugs. */
                std::fprintf(stderr,
                             "APU: unsupported SDL format %d Hz, 0x%04x, %d ch\n",
                             obtained.freq, obtained.format,
                             obtained.channels);
                SDL_CloseAudio();
            } else {
                audio_device = true;
                SDL_PauseAudio(1);
            }
        }
    }
    initialized = true;
}

extern "C" void platform_audio_shutdown(void)
{
    if (audio_device) {
        SDL_PauseAudio(1);
        SDL_CloseAudio();
        audio_device = false;
    }
    audio_started = false;
    if (audio_subsystem_started) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        audio_subsystem_started = false;
    }
    initialized = false;
}

extern "C" void platform_apu_write(uint16_t address, uint8_t value)
{
    if (!initialized)
        return;
    /* SoundEngine writes are ordered but this C port has no instruction-cycle
     * clock. Four cycles per write preserves ordering and phase-reset edges. */
    write_time += 4;
    apu.write_register(write_time, address, value);
}

extern "C" void platform_audio_frame(void)
{
    if (!initialized)
        return;

    const blip_time_t clocks = (audio_frame & 1) ? 29781 : 29780;
    apu.end_frame(clocks);
    buffer->end_frame(clocks);
    write_time = 0;
    ++audio_frame;

    blip_sample_t samples[kMaxSamples];
    long available = buffer->samples_avail();
    while (available > 0) {
        long request = available > kMaxSamples ? kMaxSamples : available;
        long count = buffer->read_samples(samples, request);
        if (count <= 0)
            break;
        queue_samples(samples, count);
        available -= count;
    }

    if (audio_device && !audio_started &&
        ring_write - ring_read >= kPrebufferSamples) {
        SDL_PauseAudio(0);
        audio_started = true;
    }
}

/* The SDL1.2 compatibility target does not emit the SDL2 verifier trace, but
 * it still implements the shared entry points so the engine remains portable. */
extern "C" void platform_audio_trace_begin(uint32_t, const uint8_t *, uint16_t) {}
extern "C" void platform_audio_trace_end(const uint8_t *, uint16_t) {}
