/* SDL host adapter for the LGPL Nes_Snd_Emu 2A03 APU core. */
#include <SDL2/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <vector>

#include "../../third_party/nes_snd_emu/nes_apu/Nes_Apu.h"
#include "../../third_party/nes_snd_emu/nes_apu/Nonlinear_Buffer.h"

namespace {
constexpr long kSampleRate = 48000;
constexpr long kCpuClock = 1789773;
constexpr int kMaxSamples = 1024;

Nes_Apu apu;
Nonlinear_Buffer buffer;
SDL_AudioDeviceID device = 0;
bool device_started = false;
blip_time_t write_time = 0;
uint32_t audio_frame = 0;
bool initialized = false;
FILE* trace_file = nullptr;
uint32_t trace_nmi = 0;
uint8_t trace_pre[64];
uint16_t trace_pre_size = 0;
struct TraceWrite { uint16_t address; uint8_t value; };
std::vector<TraceWrite> trace_writes;

int flat_dmc_reader(void*, cpu_addr_t) { return 0x55; }
}

extern "C" void platform_audio_init(void) {
    if (initialized) return;
    if (const char* path = std::getenv("SMB_AUDIO_TRACE"))
        trace_file = std::fopen(path, "wb");

    buffer.clock_rate(kCpuClock);
    /* This 2005 core's default buffer-size calculation assumes 32-bit long;
     * pass an explicit duration so 64-bit hosts do not request a huge buffer. */
    if (buffer.sample_rate(kSampleRate, 100)) {
        std::fprintf(stderr, "APU: failed to configure sample buffer\n");
        return;
    }
    buffer.enable_nonlinearity(apu, true);
    buffer.bass_freq(16);
    apu.dmc_reader(flat_dmc_reader, nullptr);

    if (SDL_WasInit(SDL_INIT_AUDIO)) {
        SDL_AudioSpec want{};
        want.freq = kSampleRate;
        want.format = AUDIO_S16SYS;
        want.channels = 1;
        want.samples = 1024;
        device = SDL_OpenAudioDevice(nullptr, 0, &want, nullptr, 0);
        /* Queue several frames before starting playback. Starting an empty
         * device makes scheduler jitter audible as note dropouts. */
        if (device) SDL_PauseAudioDevice(device, 1);
        else std::fprintf(stderr, "APU: SDL audio unavailable: %s\n", SDL_GetError());
    }
    initialized = true;
}

extern "C" void platform_audio_shutdown(void) {
    if (device) {
        SDL_ClearQueuedAudio(device);
        SDL_CloseAudioDevice(device);
        device = 0;
    }
    device_started = false;
    if (trace_file) {
        std::fclose(trace_file);
        trace_file = nullptr;
    }
    initialized = false;
}

extern "C" void platform_apu_write(uint16_t address, uint8_t value) {
    if (!initialized) return;
    /* SoundEngine writes are ordered but this C port has no instruction-cycle
     * clock. Four cycles per write preserves ordering and phase-reset edges. */
    write_time += 4;
    apu.write_register(write_time, address, value);
    if (trace_file) trace_writes.push_back({address, value});
}

extern "C" void platform_audio_trace_begin(uint32_t nmi_ordinal,
                                             const uint8_t* state,
                                             uint16_t state_size) {
    trace_nmi = nmi_ordinal;
    trace_pre_size = state_size > sizeof(trace_pre) ? sizeof(trace_pre) : state_size;
    if (trace_file && trace_pre_size) std::memcpy(trace_pre, state, trace_pre_size);
    trace_writes.clear();
}

static void print_hex(FILE* stream, const uint8_t* bytes, uint16_t size) {
    for (uint16_t i = 0; i < size; ++i) std::fprintf(stream, "%02x", bytes[i]);
}

extern "C" void platform_audio_trace_end(const uint8_t* state,
                                           uint16_t state_size) {
    if (!trace_file) return;
    std::fprintf(trace_file, "%u\t", trace_nmi);
    print_hex(trace_file, trace_pre, trace_pre_size);
    std::fputc('\t', trace_file);
    print_hex(trace_file, state, state_size);
    std::fputc('\t', trace_file);
    for (size_t i = 0; i < trace_writes.size(); ++i) {
        if (i) std::fputc(',', trace_file);
        std::fprintf(trace_file, "%04x:%02x", trace_writes[i].address,
                     trace_writes[i].value);
    }
    std::fputc('\n', trace_file);
}

extern "C" void platform_audio_frame(void) {
    if (!initialized) return;
    const blip_time_t clocks = (audio_frame & 1) ? 29781 : 29780;
    apu.end_frame(clocks);
    buffer.end_frame(clocks);
    write_time = 0;
    ++audio_frame;

    blip_sample_t samples[kMaxSamples];
    long available = buffer.samples_avail();
    if (available > kMaxSamples) available = kMaxSamples;
    if (available > 0) {
        long count = buffer.read_samples(samples, available);
        if (count > 0 && device) {
            SDL_QueueAudio(device, samples,
                           static_cast<uint32_t>(count * sizeof(samples[0])));
            if (!device_started &&
                SDL_GetQueuedAudioSize(device) >=
                    static_cast<uint32_t>((kSampleRate / 20) * sizeof(samples[0]))) {
                SDL_PauseAudioDevice(device, 0);
                device_started = true;
            }
        }
    }
}
