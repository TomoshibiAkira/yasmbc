/* DJGPP Sound Blaster host for Nes_Snd_Emu. Silent if the card is missing. */
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <dpmi.h>
#include <go32.h>
#include <pc.h>
#include <sys/movedata.h>

#include "../../third_party/nes_snd_emu/nes_apu/Nes_Apu.h"
#include "../../third_party/nes_snd_emu/nes_apu/Nonlinear_Buffer.h"

namespace {
/* DSP time constant 211 is exactly 1 MHz / 45 = 22222.22 Hz.  Generate at
 * the rate the hardware consumes instead of slowly draining the queue. */
constexpr int kTimeConstant = 211;
constexpr long kSampleRate = 1000000 / (256 - kTimeConstant);
/* Give the producer about 0.9% headroom over the nominal DSP clock.  Real
 * cards and virtual SB implementations quantize the time constant slightly
 * differently, and foreground service has frame-sized jitter.  FIFO feedback
 * below removes the surplus before it can become long-term latency. */
constexpr long kSynthesisRate = 22420;
constexpr long kCpuClock = 1789773;
constexpr int kMaxSamples = 512;
constexpr unsigned kDmaBlockBytes = 512;
constexpr unsigned kDmaBlocks = 4;
constexpr unsigned kDmaRingBytes = kDmaBlockBytes * kDmaBlocks;
constexpr unsigned kFifoBytes = 8192;

Nes_Apu apu;
Nonlinear_Buffer buffer;
blip_time_t write_time = 0;
uint32_t audio_frame = 0;
bool initialized = false;
bool sb_ok = false;
bool sb_irq_installed = false;
bool dma_started = false;

int sb_base = 0x220;
int sb_irq = 5;
int sb_dma = 1;
int dos_segment = -1;
int dos_selector = 0;
unsigned dma_linear = 0;
unsigned dma_bytes = 0;
uint8_t pcm_fifo[kFifoBytes];
unsigned fifo_read = 0;
unsigned fifo_count = 0;
unsigned refill_sequence = 0;
volatile uint32_t completed_blocks = 0;
uint32_t serviced_blocks = 0;
uint32_t underruns = 0;
uint32_t underrun_samples = 0;
uint32_t overruns = 0;
uint8_t old_pic_mask = 0xff;
uint8_t old_pic2_mask = 0xff;
_go32_dpmi_seginfo old_sb_irq;
_go32_dpmi_seginfo new_sb_irq;

int dma_addr_port[] = {0x00, 0x02, 0x04, 0x06};
int dma_count_port[] = {0x01, 0x03, 0x05, 0x07};
int dma_page_port[] = {0x87, 0x83, 0x81, 0x82};

int flat_dmc_reader(void *, cpu_addr_t) { return 0x55; }

int dsp_write(uint8_t value) {
    for (int i = 0; i < 4096; i++) {
        if ((inportb(sb_base + 0x0C) & 0x80) == 0) {
            outportb(sb_base + 0x0C, value);
            return 0;
        }
    }
    return -1;
}

void sb_irq_handler(void) {
    /* Reading the 8-bit DSP status port clears its DMA-complete interrupt. */
    inportb(sb_base + 0x0E);
    completed_blocks++;
    if (sb_irq >= 8)
        outportb(0xA0, 0x20);
    outportb(0x20, 0x20);
}

int install_sb_irq(void) {
    int vector;
    if (sb_irq < 2 || sb_irq > 15)
        return -1;
    vector = (sb_irq < 8) ? (0x08 + sb_irq) : (0x70 + sb_irq - 8);
    _go32_dpmi_get_protected_mode_interrupt_vector(vector, &old_sb_irq);
    new_sb_irq.pm_offset = (unsigned long)sb_irq_handler;
    new_sb_irq.pm_selector = _go32_my_cs();
    if (_go32_dpmi_allocate_iret_wrapper(&new_sb_irq) != 0)
        return -1;
    if (_go32_dpmi_set_protected_mode_interrupt_vector(vector, &new_sb_irq) != 0) {
        _go32_dpmi_free_iret_wrapper(&new_sb_irq);
        return -1;
    }
    sb_irq_installed = true;
    if (sb_irq < 8) {
        old_pic_mask = inportb(0x21);
        outportb(0x21, (uint8_t)(old_pic_mask & ~(1u << sb_irq)));
    } else {
        old_pic_mask = inportb(0x21);
        old_pic2_mask = inportb(0xA1);
        outportb(0x21, (uint8_t)(old_pic_mask & ~(1u << 2)));
        outportb(0xA1, (uint8_t)(old_pic2_mask & ~(1u << (sb_irq - 8))));
    }
    return 0;
}

void remove_sb_irq(void) {
    int vector;
    if (!sb_irq_installed)
        return;
    vector = (sb_irq < 8) ? (0x08 + sb_irq) : (0x70 + sb_irq - 8);
    _go32_dpmi_set_protected_mode_interrupt_vector(vector, &old_sb_irq);
    _go32_dpmi_free_iret_wrapper(&new_sb_irq);
    outportb(0x21, old_pic_mask);
    if (sb_irq >= 8)
        outportb(0xA1, old_pic2_mask);
    sb_irq_installed = false;
}

int dsp_reset(void) {
    outportb(sb_base + 0x06, 1);
    inportb(sb_base + 0x06);
    inportb(sb_base + 0x06);
    inportb(sb_base + 0x06);
    outportb(sb_base + 0x06, 0);
    for (int i = 0; i < 100000; i++) {
        if (inportb(sb_base + 0x0E) & 0x80) {
            if (inportb(sb_base + 0x0A) == 0xAA)
                return 0;
        }
    }
    return -1;
}

void parse_blaster(void) {
    const char *env = std::getenv("BLASTER");
    if (!env)
        return;
    while (*env) {
        char key = *env++;
        if (key >= 'a' && key <= 'z')
            key = (char)(key - 'a' + 'A');
        int value = 0;
        int hex = (key == 'A' || key == 'P' || key == 'E');
        while (*env == ' ')
            env++;
        while (*env && *env != ' ') {
            char c = *env++;
            if (c >= 'a' && c <= 'f')
                c = (char)(c - 'a' + 'A');
            if (hex) {
                value <<= 4;
                if (c >= '0' && c <= '9')
                    value += c - '0';
                else if (c >= 'A' && c <= 'F')
                    value += c - 'A' + 10;
            } else if (c >= '0' && c <= '9') {
                value = value * 10 + (c - '0');
            }
        }
        if (key == 'A')
            sb_base = value;
        else if (key == 'I')
            sb_irq = value;
        else if (key == 'D')
            sb_dma = value;
        while (*env == ' ')
            env++;
    }
}

int alloc_dma_buffer(unsigned want) {
    int extra = 65536 / 16;
    int paras = (int)((want + 15) / 16) + extra;
    dos_segment = __dpmi_allocate_dos_memory(paras, &dos_selector);
    if (dos_segment == -1)
        return -1;
    unsigned start = (unsigned)dos_segment << 4;
    unsigned aligned = (start + 0xFFFF) & ~0xFFFFu;
    if (aligned - start >= want)
        dma_linear = start;
    else
        dma_linear = aligned;
    if ((dma_linear & 0xFFFF) + want > 0x10000)
        dma_linear = (dma_linear + 0xFFFF) & ~0xFFFFu;
    dma_bytes = want;
    return 0;
}

void dma_program_auto(unsigned linear, unsigned length) {
    unsigned page = linear >> 16;
    unsigned offset = linear & 0xFFFF;
    unsigned count = length - 1;
    int ch = sb_dma & 3;
    outportb(0x0A, (uint8_t)(4 | ch));
    outportb(0x0C, 0);
    /* Single transfer mode, auto initialize, memory-to-device. */
    outportb(0x0B, (uint8_t)(0x58 | ch));
    outportb(dma_addr_port[ch], (uint8_t)offset);
    outportb(dma_addr_port[ch], (uint8_t)(offset >> 8));
    outportb(dma_page_port[ch], (uint8_t)page);
    outportb(dma_count_port[ch], (uint8_t)count);
    outportb(dma_count_port[ch], (uint8_t)(count >> 8));
    outportb(0x0A, (uint8_t)ch);
}

int sb_start(void) {
    parse_blaster();
    if (sb_dma > 3)
        sb_dma = 1;
    if (dsp_reset() != 0)
        return -1;
    if (alloc_dma_buffer(kDmaRingBytes) != 0)
        return -1;
    if (install_sb_irq() != 0)
        return -1;
    if (dsp_write(0xD1) != 0)
        return -1;
    {
        if (dsp_write(0x40) != 0 || dsp_write((uint8_t)kTimeConstant) != 0)
            return -1;
    }
    return 0;
}

void fifo_push(const blip_sample_t *samples, long count) {
    long i;
    int shed = fifo_count > kDmaBlockBytes * 2 ? 3 : 0;
    for (i = 0; i < count; i++) {
        int s = samples[i] / 256 + 128;
        unsigned write;
        if (s < 0)
            s = 0;
        if (s > 255)
            s = 255;
        if (shed && i < shed)
            continue;
        if (fifo_count == kFifoBytes) {
            fifo_read = (fifo_read + 1) % kFifoBytes;
            fifo_count--;
            overruns++;
        }
        write = (fifo_read + fifo_count) % kFifoBytes;
        pcm_fifo[write] = (uint8_t)s;
        fifo_count++;
    }
}

void fill_dma_block(unsigned block) {
    uint8_t output[kDmaBlockBytes];
    unsigned copied = 0;
    while (copied < kDmaBlockBytes && fifo_count) {
        output[copied++] = pcm_fifo[fifo_read];
        fifo_read = (fifo_read + 1) % kFifoBytes;
        fifo_count--;
    }
    if (copied < kDmaBlockBytes) {
        memset(output + copied, 128, kDmaBlockBytes - copied);
        underruns++;
        underrun_samples += kDmaBlockBytes - copied;
    }
    dosmemput(output, kDmaBlockBytes, dma_linear + block * kDmaBlockBytes);
}

void start_dma_ring(void) {
    unsigned block;
    if (dma_started || fifo_count < kDmaRingBytes)
        return;
    for (block = 0; block < kDmaBlocks; block++)
        fill_dma_block(block);
    completed_blocks = serviced_blocks = refill_sequence = 0;
    dma_program_auto(dma_linear, kDmaRingBytes);
    /* SB 2.0+ 8-bit auto-init: block length then start output. */
    if (dsp_write(0x48) != 0 || dsp_write((uint8_t)(kDmaBlockBytes - 1)) != 0 ||
        dsp_write((uint8_t)((kDmaBlockBytes - 1) >> 8)) != 0 || dsp_write(0x1C) != 0) {
        sb_ok = false;
        outportb(0x0A, (uint8_t)(4 | (sb_dma & 3)));
        return;
    }
    dma_started = true;
}

void service_dma_ring(void) {
    uint32_t completed;
    if (!dma_started) {
        start_dma_ring();
        return;
    }
    completed = completed_blocks;
    while (serviced_blocks != completed) {
        uint32_t free_blocks = completed - serviced_blocks;
        /* Even with three released blocks, the DMA is still playing the
         * fourth for about 23 ms.  At near-60 Hz the next game frame arrives
         * in time, so do not manufacture a tiny silence gap.  Only pad after
         * the DMA has genuinely lapped the producer by a complete ring. */
        if (fifo_count < kDmaBlockBytes && free_blocks < kDmaBlocks)
            break;
        fill_dma_block(refill_sequence);
        refill_sequence = (refill_sequence + 1) % kDmaBlocks;
        serviced_blocks++;
    }
}
} // namespace

extern "C" void platform_audio_init(void) {
    if (initialized)
        return;
    buffer.clock_rate(kCpuClock);
    if (buffer.sample_rate(kSynthesisRate, 100)) {
        std::fprintf(stderr, "APU: failed to configure sample buffer\n");
        return;
    }
    buffer.enable_nonlinearity(apu, true);
    buffer.bass_freq(16);
    apu.dmc_reader(flat_dmc_reader, nullptr);
    audio_frame = 0;
    write_time = 0;
    fifo_read = fifo_count = 0;
    dma_started = false;
    completed_blocks = serviced_blocks = refill_sequence = 0;
    underruns = underrun_samples = overruns = 0;
    sb_ok = (sb_start() == 0);
    initialized = true;
}

extern "C" void platform_audio_shutdown(void) {
    if (sb_ok) {
        dsp_write(0xDA); /* exit 8-bit auto-init after the current block */
        dsp_write(0xD3);
        dsp_write(0xD0);
        outportb(0x0A, (uint8_t)(4 | (sb_dma & 3)));
    }
    remove_sb_irq();
    if (dos_segment != -1) {
        __dpmi_free_dos_memory(dos_selector);
        dos_segment = -1;
    }
    sb_ok = false;
    dma_started = false;
    initialized = false;
}

extern "C" void platform_apu_write(uint16_t address, uint8_t value) {
    if (!initialized || !sb_ok)
        return;
    write_time += 4;
    apu.write_register(write_time, address, value);
}

extern "C" void platform_audio_trace_begin(uint32_t, const uint8_t *, uint16_t) {}
extern "C" void platform_audio_trace_end(const uint8_t *, uint16_t) {}

extern "C" void platform_audio_frame(void) {
    if (!initialized || !sb_ok)
        return;
    const blip_time_t clocks = (audio_frame & 1) ? 29781 : 29780;
    apu.end_frame(clocks);
    buffer.end_frame(clocks);
    write_time = 0;
    ++audio_frame;

    blip_sample_t samples[kMaxSamples];
    long available = buffer.samples_avail();
    if (available > kMaxSamples)
        available = kMaxSamples;
    if (available > 0) {
        long count = buffer.read_samples(samples, available);
        if (count > 0 && sb_ok) {
            fifo_push(samples, count);
            service_dma_ring();
        }
    }
}

extern "C" uint32_t platform_audio_underruns(void) { return underruns; }
extern "C" uint32_t platform_audio_underrun_samples(void) { return underrun_samples; }
extern "C" uint32_t platform_audio_overruns(void) { return overruns; }
extern "C" uint32_t platform_audio_dma_blocks(void) { return completed_blocks; }
extern "C" uint32_t platform_audio_fifo_samples(void) { return fifo_count; }
