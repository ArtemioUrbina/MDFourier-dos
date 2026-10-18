/*
 * PCM through the Sound Blaster DSP
 * 8-bit mono on every DSP
 * 8-bit stereo on DSP 3.xx (SB Pro method) and 4.xx (SB16 commands)
 * 16-bit stereo on DSP 4.xx with a high DMA channel.
 *
 * MDF.C steps through the sequence one VGA frame at a time.
 * Every tone starts with a single DSP command sent after a frame edge
 * so it lines up with the frames the same way on all sound cards.
 *
 * The DSP plays from memory through DMA. While one tone plays, the
 * next one is built into a second buffer, a chunk per frame, because
 * building a whole tone at once takes longer than a frame on a 486.
 * 
 * Each transfer raises one IRQ when it ends. Counting them tells if
 * the card really played what it was asked to.
 *
 * Sources:
 * https://pdos.csail.mit.edu/6.828/2017/readings/hardware/SoundBlaster.pdf
 * Intel 8237A data sheet (DMA), Intel 8259A data sheet (PIC)
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <conio.h>
#include <dos.h>
#include <i86.h>
#include "pcm.h"
#include "dsp.h"
#include "mixer.h"


#define DSP_READ_READY  0x0E    /* reading it acks the 8-bit IRQ */
#define DSP_ACK_16      0x0F    /* reading it acks the 16-bit IRQ, DSP 4.xx */
#define SB16_IRQ_STATUS 0x82    /* mixer: bit 0 8-bit, bit 1 16-bit */

#define DSP_SPEAKER_ON  0xD1
#define DSP_SPEAKER_OFF 0xD3
#define DSP_TIME_CONST  0x40
#define DSP_PLAY_8BIT   0x14    /* single-cycle, 8-bit mono, every DSP */

/* Rate = 1000000 / (256 - TC). TC 211 gives 22222.2 Hz, in range for
 * normal mode on every DSP version */
#define PCM_TC          211
#define PCM_RATE        (1000000.0 / 45.0)

/* Tones fill this share of their block. The rest is silence, so a
 * tone is over before the next one starts even on the slowest card
 * measured, which plays 2.2% long. The silence is also a decay. */
#define FILL_PCT        85

/* Clock tone: rate / 8 (2777.8 Hz), below any card's output filter.
 * Its frequency measures the card's crystal: MDFourier reads it and
 * names the card family from it. Each 8-sample period ends on 0x80,
 * so the DAC rests at the midpoint whenever it stops (no click).
 * The block needs ~18000 samples, the buffer holds CLK_MAX.
 * DMA loops it in auto-init while the DSP plays one
 * single-cycle transfer of ~CLK_PCT of the block.  */
#define CLK_PERIOD      8
#define CLK_MAX         5600
#define CLK_PCT         95

/* Step tones. Level at rate / 22 so every period is the same 22
 * samples, no table jitter from one period to the next.
 * Single-cycle transfers start and stop at full level.  */
#define SWEEP_LOW       20.0
#define SWEEP_HIGH      10000.0
#define LEVEL_FREQ      (PCM_RATE / 22.0)   /* 1010.1 Hz */
#define FADE            32                  /* samples, in and out */

/* Sweep and clock 3 dB below full scale, so the first Level step
 * (full scale, 1 kHz) is always the loudest PCM element. MDFourier
 * normalizes to the loudest one, it must not depend on each card's
 * bass or treble response */
#define SWEEP_AMP       181                 /* of 256, -3.0 dB */
#define CLK_AMP         90.0                /* of 127, -3.0 dB */
#define STEP_MAX        5600                /* 20 frames at 85% */

/* Samples built per frame. A 20-frame step needs 8 frames, a 10-frame
 * one 4: a few ms per frame, even without an FPU */
#define FILL_CHUNK      768

/* Sine table, 1024 entries, +-127.
 * A power of two, so the top bits of the phase accumulator index it directly */
#define SINE_BITS       10
#define SINE_SIZE       (1 << SINE_BITS)
int8_t sine[SINE_SIZE];

/* The sine at each step amplitude, built once.
 * 8 Level steps and the sweep. */
#define AMP_TABLES      (PCM_LEVEL_STEPS + 1)
#define AMP_SWEEP_TAB   PCM_LEVEL_STEPS
int8_t amp_sine[AMP_TABLES][SINE_SIZE];

/* DMA buffers: clock, then two steps (one plays while the other is
 * built). From buf_alloc(), with the stereo ones */
uint8_t __far *clk_buf;
uint8_t __far *step_buf[2];
uint16_t step_len[2];

/* 8237 ports, channels 0-3. The page registers (address bits 16-23)
 * are in this odd order since the original PC */
#define DMA_MASK        0x0A
#define DMA_MODE        0x0B
#define DMA_FLIPFLOP    0x0C
#define DMA_MODE_SINGLE 0x48    /* single, increment, memory to card */
#define DMA_MODE_AUTO   0x58    /* same, auto-initialize */
const uint8_t dma_page_port[4] = { 0x87, 0x83, 0x81, 0x82 };

/* 8259 ports */
#define PIC1_CMD        0x20
#define PIC1_DATA       0x21
#define PIC2_CMD        0xA0
#define PIC2_DATA       0xA1
#define PIC_EOI         0x20

int       ready;            /* pcm_init() passed, everything else checks it */
uint16_t  base;
uint8_t   irq, dma;
uint8_t   dsp_major;        /* picks the commands: 1.xx to 4.xx differ */
double    frame_rate;       /* measured, sizes the tones to the blocks */

/* Put back at exit so DOS keeps working */
void (__interrupt __far *old_vector)();
uint8_t   old_pic1, old_pic2;
int       installed;

/* Transfers ended, from the ISR. */
volatile unsigned irq_count;

uint16_t          clk_len;       /* clock samples, a whole number of periods */
int               playing;       /* a transfer may be running */

/* Results */
typedef struct {
    const char *name;
    unsigned    expected;
    unsigned    got;
} pcm_count_t;

pcm_count_t count_clock = { "clock", 0, 0 };
pcm_count_t count_steps[2] = { { "sweep", 0, 0 }, { "level", 0, 0 } };

/* irq_count when the step started: still the same at the next start
 * means its IRQ never came */
unsigned step_irq_base;
int      step_pending;

int  buf_alloc(int stereo);
void st_init(blaster_cfg_t *cfg);
void st_fill_samples(unsigned max_frames);
void st_finish();
int  st_print_report();

/* IRQ 0-7 are vectors 0x08-0x0F, IRQ 8-15 (second PIC) 0x70-0x77 */
unsigned vector_of(uint8_t irq_line) {
    return irq_line < 8 ? 0x08 + irq_line : 0x70 + (irq_line - 8);
}

/* One IRQ per transfer, at its end. The DSP keeps its IRQ line raised
 * until acked by reading a port, without that no further IRQ comes.
 * DSP 4.xx has two sources on the one line, 8-bit and 16-bit transfers,
 * each acked on its own port, mixer 0x82 tells which. If it reads
 * neither (a clone without it), ack the 8-bit one */
void __interrupt __far pcm_isr() {
    if (dsp_major >= 4) {
        uint8_t irq_status = mix_read(base, SB16_IRQ_STATUS);

        if (irq_status & 2)
            inp(base + DSP_ACK_16);
            
        if ((irq_status & 1) || !(irq_status & 3))
            inp(base + DSP_READ_READY);
    } else
        inp(base + DSP_READ_READY);

    irq_count++;

    /* End of interrupt, to the second PIC as well for IRQ 8-15. 
     * It reaches the CPU through the first one (cascade on IRQ 2) */
    if (irq >= 8)
        outp(PIC2_CMD, PIC_EOI);

    outp(PIC1_CMD, PIC_EOI);
}

/* Real mode address: segment * 16 + offset. DMA needs the physical one */
uint32_t phys_of(void __far *ptr) {
    return ((uint32_t)FP_SEG(ptr) << 4) + FP_OFF(ptr);
}

void build_tables() {
    unsigned pos, table;

    for (pos = 0; pos < SINE_SIZE; pos++)
        sine[pos] = (int8_t)floor(127.0 * sin(2.0 * 3.14159265358979 * pos / SINE_SIZE) + 0.5);

    for (table = 0; table < AMP_TABLES; table++) {
        /* Of 256: halving the amplitude is 6 dB per Level step */
        int amp = table == AMP_SWEEP_TAB ? SWEEP_AMP : (256 >> table);

        for (pos = 0; pos < SINE_SIZE; pos++) {
            int scaled = sine[pos] * amp;      /* +-127 * 256 fits an int */

            /* Rounded away from zero on both sides, so the negative
             * half mirrors the positive one. No DC offset */
            amp_sine[table][pos] = (int8_t)((scaled + (scaled >= 0 ? 128 : -128)) / 256);
        }
    }

    /* sin at rate / 8 starting at 45 degrees: the period's last
     * sample is 0x80 and the next one carries on the same sine */
    for (pos = 0; pos < CLK_MAX; pos++)
        clk_buf[pos] = (uint8_t)(128 + floor(CLK_AMP *
                       sin(2.0 * 3.14159265358979 * ((pos % CLK_PERIOD) + 1) / CLK_PERIOD) + 0.5));
}

/* 8237, channels 0-3. Masked while programming so it can't start
 * halfway. The flip-flop picks which byte of a 16-bit register the
 * next write goes to. 
 * Cleared, low byte first.
 * The count register takes length - 1, it transfers until it counts past 0 */
void dma_program(uint8_t __far *buf, uint16_t len, uint8_t mode) {
    uint32_t phys        = phys_of(buf);
    uint16_t page_offset = (uint16_t)(phys & 0xFFFF);
    uint16_t len_minus_1 = (uint16_t)(len - 1);

    outp(DMA_MASK, 0x04 | dma);
    outp(DMA_FLIPFLOP, 0);
    outp(DMA_MODE, mode | dma);
    outp(dma * 2, page_offset & 0xFF);
    outp(dma * 2, page_offset >> 8);
    outp(dma * 2 + 1, len_minus_1 & 0xFF);
    outp(dma * 2 + 1, len_minus_1 >> 8);
    outp(dma_page_port[dma], (uint8_t)(phys >> 16));
    outp(DMA_MASK, dma);
}

void dma_mask() {
    outp(DMA_MASK, 0x04 | dma);
}

/* A DSP command with a length, which the DSP also takes as length - 1 */
int dsp_play(uint8_t cmd, uint16_t len) {
    uint16_t len_minus_1 = (uint16_t)(len - 1);

    return dsp_write(base, cmd) &&
           dsp_write(base, (uint8_t)(len_minus_1 & 0xFF)) &&
           dsp_write(base, (uint8_t)(len_minus_1 >> 8));
}

/* A transfer left running. Stop it and get the DSP back. The reset
 * stops anything but forgets the rate and the speaker.
 * The rate goes before the speaker on: on DSP 1.xx/2.xx that one takes ~112 ms and
 * can drop what follows it */
void recover() {
    dma_mask();
    dsp_reset(base);
    dsp_write(base, DSP_TIME_CONST);
    dsp_write(base, PCM_TC);
    dsp_write(base, DSP_SPEAKER_ON);
    playing = 0;
}

void pcm_uninstall() {
    if (!installed)
        return;

    _disable();

    if (irq >= 8)
        outp(PIC2_DATA, old_pic2);
    outp(PIC1_DATA, old_pic1);
    _dos_setvect(vector_of(irq), old_vector);

    _enable();

    installed = 0;
}

/* Our handler on the card's IRQ, unmasked at the PIC. IRQ 8-15 also
 * need IRQ 2 unmasked on the first PIC, the second one goes through it */
void pcm_install() {
    _disable();

    old_vector = _dos_getvect(vector_of(irq));
    _dos_setvect(vector_of(irq), pcm_isr);
    old_pic1 = (uint8_t)inp(PIC1_DATA);
    if (irq >= 8) {
        old_pic2 = (uint8_t)inp(PIC2_DATA);
        outp(PIC2_DATA, old_pic2 & ~(1 << (irq - 8)));
        outp(PIC1_DATA, old_pic1 & ~(1 << 2));      /* cascade */
    } else
        outp(PIC1_DATA, old_pic1 & ~(1 << irq));

    _enable();

    installed = 1;
    atexit(pcm_uninstall);
}

int pcm_init(blaster_cfg_t *cfg, double frame_hz) {
    dsp_version_t ver;

    ready = 0;
    if (cfg->port == -1 || cfg->irq == -1 || cfg->dma == -1) {
        printf("PCM: BLASTER needs A, I and D\n");
        return 0;
    }
    if (cfg->dma > 3 || cfg->irq < 2 || cfg->irq > 15) {
        printf("PCM: unsupported I%d or D%d\n", cfg->irq, cfg->dma);
        return 0;
    }
    base = (uint16_t)cfg->port;
    /* On an AT, IRQ 2 is the cascade: cards on the IRQ 2 pin arrive as 9 */
    irq  = (uint8_t)(cfg->irq == 2 ? 9 : cfg->irq);
    dma  = (uint8_t)cfg->dma;
    frame_rate = frame_hz;

    if (!dsp_reset(base) || !dsp_get_version(base, &ver)) {
        printf("PCM: DSP not answering\n");
        return 0;
    }

    dsp_major = ver.major;

    if (!buf_alloc(dsp_major >= 3)) {
        printf("PCM: no memory for buffers\n");
        return 0;
    }

    /* Slow on a 486 SX since sin() is emulated, alert the user we are working */
    printf("PCM: building tables...\n");
    build_tables();
    pcm_install();
    ready = 1;

    printf("PCM: 8-bit mono, %.1f Hz, IRQ %u, DMA %u\n", PCM_RATE, irq, dma);
    st_init(cfg);
    return 1;
}

/* On DSP 1.xx/2.xx the speaker on takes ~112 ms and the DSP drops commands meanwhile */
void pcm_prepare() {
    if (!ready)
        return;

    dsp_write(base, DSP_SPEAKER_ON);
}

/* ---- Clock tone ---- */

/* In the gap's last frame: everything but the start, so the start on
 * the frame edge is a single DSP command, as quick as it gets */
void pcm_clock_arm(unsigned frames) {
    unsigned long total;

    if (!ready)
        return;

    /* Whole periods, so it ends on 0x80. Under 64 KB, one DSP count */
    total = (unsigned long)(PCM_RATE * frames * CLK_PCT / 100.0 / frame_rate);
    total -= total % CLK_PERIOD;
    if (total > 0xFFF8UL)
        total = 0xFFF8UL;
    clk_len = (uint16_t)total;
    count_clock.expected = 1;

    /* Rate set here, frames after the speaker on. The DMA loops the
     * buffer, the DSP count says when to stop */
    dsp_write(base, DSP_TIME_CONST);
    dsp_write(base, PCM_TC);
    dma_program(clk_buf, CLK_MAX, DMA_MODE_AUTO);
}

/* On the frame edge, one single-cycle transfer for the whole tone */
void pcm_clock_start() {
    if (!ready)
        return;

    irq_count = 0;
    if (dsp_play(DSP_PLAY_8BIT, clk_len))
        playing = 1;
}

/* One IRQ should have come */
void pcm_clock_stop() {
    if (!ready)
        return;

    count_clock.got = irq_count;

    if (irq_count >= 1) {
        dma_mask();
        playing = 0;
    } else
        recover();
}

/* ---- Steps ---- */

/* Log spaced. The same ratio between steps, like the FM sweep */
double step_freq(pcm_steps_t kind, unsigned step) {
    if (kind == PCM_LEVEL)
        return LEVEL_FREQ;
    return SWEEP_LOW * pow(SWEEP_HIGH / SWEEP_LOW, (double)step / (PCM_SWEEP_STEPS - 1));
}

/* The step being built, a chunk per frame.
 * Phase is a 32-bit accumulator. A full turn of the sine is 2^32, each
 * sample adds phase_inc = freq / rate * 2^32.
 * Its top SINE_BITS bits index the table.
 * Any frequency, precise to ~5 uHz with no floating point per sample */
struct {
    int            active;
    unsigned       buf;
    uint8_t __far *dest;
    const int8_t  *table;
    unsigned       len, pos;
    uint32_t       phase, phase_inc;
} fill;

void fill_samples(unsigned max_samples) {
    unsigned end, pos;

    if (!fill.active)
        return;

    end = fill.pos + max_samples;
    if (end > fill.len)
        end = fill.len;

    for (pos = fill.pos; pos < end; pos++) {
        int sample = fill.table[(uint16_t)(fill.phase >> 16) >> (16 - SINE_BITS)];

        if (pos < FADE)
            sample = sample * (int)pos / FADE;
        else if (pos >= fill.len - FADE)
            sample = sample * (int)(fill.len - 1 - pos) / FADE;
        fill.dest[pos] = (uint8_t)(128 + sample);   /* 8-bit PCM is unsigned, 128 = 0 */
        fill.phase += fill.phase_inc;
    }
    fill.pos = end;
    if (fill.pos >= fill.len)
        fill.active = 0;
}

/* Sets up the build */
void pcm_step_fill(pcm_steps_t kind, unsigned step, unsigned buf, unsigned frames) {
    unsigned len;

    if (!ready)
        return;

    len = (unsigned)(PCM_RATE * frames * FILL_PCT / 100.0 / frame_rate);
    if (len > STEP_MAX)
        len = STEP_MAX;
    step_len[buf] = (uint16_t)len;

    fill.buf       = buf;
    fill.dest      = step_buf[buf];
    fill.len       = len;
    fill.pos       = 0;
    fill.phase     = 0;
    fill.phase_inc = (uint32_t)(step_freq(kind, step) * 4294967296.0 / PCM_RATE);
    fill.table     = amp_sine[kind == PCM_LEVEL ? step : AMP_SWEEP_TAB];
    fill.active    = 1;
}

void pcm_fill_more() {
    fill_samples(FILL_CHUNK);
    st_fill_samples(FILL_CHUNK);
}

/* Counts the previous step's IRQ, then starts this
 * one as a single-cycle transfer, one IRQ at its end */
void pcm_step_start(pcm_steps_t kind, unsigned buf) {
    pcm_count_t *count = &count_steps[kind];

    if (!ready)
        return;

    if (step_pending) {
        if (irq_count != step_irq_base)
            count->got++;
        else
            recover();
    }

    /* Should be done by now, the rest if not. It costs frame time,
     * only on a CPU that can't keep up */
    if (fill.active && fill.buf == buf)
        fill_samples(STEP_MAX);

    step_irq_base = irq_count;
    dma_program(step_buf[buf], step_len[buf], DMA_MODE_SINGLE);
    count->expected++; 
    step_pending = dsp_play(DSP_PLAY_8BIT, step_len[buf]);
    playing = step_pending;
}

void pcm_steps_end(pcm_steps_t kind) {
    pcm_count_t *count = &count_steps[kind];

    if (!ready || !step_pending)
        return;

    if (irq_count != step_irq_base) {
        count->got++;
        dma_mask();
        playing = 0;
    } else
        recover();

    step_pending = 0;
}

/* A DSP reset clicks only if something may still be playing */
void pcm_finish() {
    if (!ready)
        return;

    st_finish();
    dma_mask();

    if (playing)
        dsp_reset(base);

    dsp_write(base, DSP_SPEAKER_OFF);
    playing = 0;
}

int print_count(pcm_count_t *count) {
    if (!count->expected)
        return 1;

    printf(" %s %u/%u", count->name, count->got, count->expected);
    return count->got == count->expected;
}

void pcm_print_report() {
    int ok;

    if (!ready)
        return;

    printf("  PCM IRQs:");
    ok  = print_count(&count_clock);
    ok &= print_count(&count_steps[PCM_SWEEP]);
    ok &= print_count(&count_steps[PCM_LEVEL]);
    printf("\n");
    ok &= st_print_report();

    if (!ok)
        printf("  WARNING: PCM IRQ %u missing, check BLASTER I and the card's jumpers\n", irq);
}

/* ======================================================================
 * Stereo 8-bit and 16-bit
 *
 * Each tone is a loop buffer holding whole sine cycles. The DMA runs in
 * auto-init over the buffer and the DSP plays one single-cycle transfer
 * of a whole number of loops, so it ends on a cycle boundary (silence)
 * with one IRQ. Whole cycles start and end at 0, no fade needed. The
 * clock tones are played the same way, from their own buffers
 *
 * 8-bit stereo, DSP 3.xx: SB Pro method, as in Creative's programming
 * guide ("8-bit Stereo High-speed Single-cycle Transfer"): mixer 0x0E
 * stereo on and output filter off, one silent byte so the channels start
 * in order, then high-speed transfers at 1 MHz/23 bytes/s (21739 Hz per
 * channel). Mixer 0x0E restored by pcm_finish().
 * 8-bit stereo, DSP 4.xx: 0xC0 with the stereo mode bit, 22050 Hz.
 * 16-bit stereo, DSP 4.xx: 0xB0 signed stereo, 44100 Hz, high DMA.
 *
 * The rate goes with every transfer. DSPs take it right away, and
 * after a recover() it is back without anything to track.
 *
 * A frame below is one sample per channel
 * 2 bytes in 8-bit stereo.
 * 2 words in 16-bit. Left first
 * ====================================================================== */

#define DSP_RATE_OUT    0x41    /* DSP 4.xx output rate, high byte first */
#define DSP_SB16_8BIT   0xC0    /* DSP 4.xx 8-bit single-cycle, FIFO off */
#define DSP_SB16_16BIT  0xB0    /* DSP 4.xx 16-bit single-cycle, FIFO off */
#define MODE_U8_STEREO  0x20
#define MODE_S16_STEREO 0x30
#define DSP_HS_8BIT     0x91    /* DSP 3.xx high-speed 8-bit single-cycle */
#define DSP_BLOCK_SIZE  0x48    /* its length, sent before it */
#define SBP_OUT_CTRL    0x0E    /* SB Pro mixer, bit 1 stereo, bit 5 filter off */
#define SBP_STEREO      0x02
#define SBP_FILTER_OFF  0x20

/* SB Pro stereo takes bytes at twice the per-channel rate, left and
 * right in turn. 43478 bytes/s is over the ~23 kHz of normal mode.
 * Needs the high-speed command */
#define ST_RATE16       44100u
#define ST_RATE8        22050u  /* per channel, DSP 4.xx */
#define ST_TC8          233     /* DSP 3.xx: 43478 bytes/s, 21739 per channel */

/* Loop buffer, frames: 121 ms at 44.1 kHz, 85% of a 10-frame block, so
 * a Sine step is one loop and a 20-frame Level or Separation step two. At
 * 22.05 kHz the same buffer lasts 243 ms, one loop for a 20-frame step */
#define ST_STEP_L       5355u
#define ST_CLK_L        4096u   /* whole rate / 8 periods */

/* Level and Separation tones at full scale: if a card's stereo or 16-bit
 * path is louder than its 8-bit mono one, that is the hardware and
 * MDFourier documents it. Sweep and clocks 3 dB down, as in 8-bit mono,
 * so the reference stays on a 1 kHz step and not on whichever sweep
 * frequency a card boosts */
#define ST_AMP_TONE     32767   /* 0 dB: Separation and the first Level step */
#define ST_AMP_SWEEP    23198   /* -3 dB: sweep and clocks */
#define ST_LEVEL_SHIFT  2       /* 12 dB per Level step */
#define ST_TONE_CYCLES8  243    /* 1000.6 Hz at 22050, in ST_STEP_L frames */
#define ST_TONE_CYCLES16 121    /*  996.4 Hz at 44100 */
#define ST_SWEEP_LOW    25.0
#define ST_SWEEP_HIGH   20000.0

/* Second 8237, channels 5-7. It moves 16-bit words: addresses are in
 * words (physical / 2) within 128 KB pages, page register bit 0 unused */
#define DMA16_MASK      0xD4
#define DMA16_MODE      0xD6
#define DMA16_FLIPFLOP  0xD8
const uint8_t dma16_page_port[4] = { 0x8F, 0x8B, 0x89, 0x8A };

/* Built once, steps only shift or look them up. No multiply per
 * sample: a 32-bit one is a library call on a 16-bit CPU */
#define ST_SINE_BITS    10
#define ST_SINE_SIZE    (1 << ST_SINE_BITS)
int16_t st_sine16[ST_SINE_SIZE];     /* ST_AMP_TONE */
int16_t st_sine16_3db[ST_SINE_SIZE]; /* ST_AMP_SWEEP */
uint8_t st_sine8[ST_SINE_SIZE];      /* ST_AMP_TONE, unsigned 8-bit */

/* Sweep cycles per loop buffer, log spaced, one more than the step
 * before at least */
unsigned st_sweep_cycles[PCMX_SWEEP_STEPS];

int      st_stereo_ok, st16_ok;
uint8_t  hdma;
int      st_sbpro;               /* DSP 3.xx path */
int      st_sbp_saved = -1;      /* mixer 0x0E before stereo */
uint8_t  __far prime_byte = 0x80;

uint8_t __far *st_clk8;          /* ST_CLK_L stereo 8-bit frames */
int16_t __far *st_clk16;         /* ST_CLK_L stereo 16-bit frames */
uint8_t __far *st_step[2];       /* ST_STEP_L stereo 16-bit frames each */
unsigned st_next;                /* buffer the next step goes into */

typedef struct {
    pcmx_section_t section;
    int            bits16;
    int            clock;               /* plays its own clock buffer */
    unsigned       loops;               /* buffers played per start */
} st_kind_info_t;

/* Clocks: 4 loops of 4096 frames at 22050 Hz, 8 at 44100, both ~0.74 s
 * of the 0.86 s block. 8 loops of 16-bit stereo are 65536 words, the
 * most one DSP count can hold */
const st_kind_info_t st_info[PCMX_KIND_COUNT] = {
    { PCMX_STEREO, 0, 1, 4 },           /* PCMX_CLK8,   ~0.74 s */
    { PCMX_STEREO, 0, 0, 1 },           /* PCMX_SEP8 */
    { PCMX_16,     1, 1, 8 },           /* PCMX_CLK16,  ~0.74 s */
    { PCMX_16,     1, 0, 1 },           /* PCMX_SWEEP16 */
    { PCMX_16,     1, 0, 2 },           /* PCMX_LEVEL16 */
    { PCMX_16,     1, 0, 2 },           /* PCMX_SEP16 */
};

pcm_count_t st_count[PCMX_KIND_COUNT] = {
    { "clock", 0, 0 }, { "separation", 0, 0 },
    { "clock", 0, 0 }, { "sweep", 0, 0 }, { "level", 0, 0 }, { "separation", 0, 0 }
};

unsigned            st_irq_base;
int                 st_pending = -1;        /* kind playing, its IRQ not counted */

/* The step being built, a chunk per frame. Same phase accumulator as
 * fill, over the loop buffer: phase_inc = cycles / ST_STEP_L * 2^32,
 * so the buffer holds exactly that many cycles */
struct {
    int             active;
    int             bits16;
    uint8_t __far   *dest;
    const int16_t   *table;
    unsigned        pos;
    unsigned        channels;           /* bit 0 left, bit 1 right */
    unsigned        level_shift;        /* 6 dB per bit */
    uint32_t        phase, phase_inc;
} sf;

/* ---- Memory: one DOS block, buffers placed so none crosses 64 KB ----
 * The 8237 can't cross a 64 KB physical page: its page register
 * doesn't carry. With stereo the buffers are ~75 KB, more than the
 * small model's single 64 KB data segment can spare: they come from
 * DOS, and each is placed so it stays within one page */

uint32_t buf_cursor, buf_end;   /* physical: next free byte, end of the block */

/* The next free spot that doesn't cross a 64 KB page, on an even
 * address since 16-bit DMA moves words */
void __far *buf_place(uint32_t bytes) {
    uint32_t phys = (buf_cursor + 1) & ~1UL;

    if ((phys >> 16) != ((phys + bytes - 1) >> 16))
        phys = (phys + 0xFFFFUL) & ~0xFFFFUL;      /* start of the next page */

    if (phys + bytes > buf_end)
        return 0;

    buf_cursor = phys + bytes;
    return MK_FP((unsigned)(phys >> 4), (unsigned)(phys & 15));
}

/* Mono buffers always, stereo ones from DSP 3.xx. A buffer that would
 * cross a page starts at the next one: each one under 21.5 KB, the
 * skips waste less than 64 KB in total */
#define MONO_NEED       (CLK_MAX + 2UL * STEP_MAX)
#define STEREO_NEED     (2UL * ST_STEP_L * 4 + ST_CLK_L * 4 + ST_CLK_L * 2)

int buf_alloc(int stereo) {
    unsigned seg;
    uint32_t total = MONO_NEED + (stereo ? STEREO_NEED : 0) + 0x10000UL;

    /* In 16-byte paragraphs */
    if (_dos_allocmem((unsigned)((total + 15) >> 4), &seg) != 0)
        return 0;

    buf_cursor = (uint32_t)seg << 4;
    buf_end    = buf_cursor + total;

    clk_buf     = (uint8_t __far *)buf_place(CLK_MAX);
    step_buf[0] = (uint8_t __far *)buf_place(STEP_MAX);
    step_buf[1] = (uint8_t __far *)buf_place(STEP_MAX);
    if (!clk_buf || !step_buf[0] || !step_buf[1])
        return 0;

    if (stereo) {
        st_step[0] = (uint8_t __far *)buf_place(ST_STEP_L * 4UL);
        st_step[1] = (uint8_t __far *)buf_place(ST_STEP_L * 4UL);
        st_clk16   = (int16_t __far *)buf_place(ST_CLK_L * 4UL);
        st_clk8    = (uint8_t __far *)buf_place(ST_CLK_L * 2UL);
    }
    return 1;
}

void st_build_tables() {
    unsigned pos, prev_cycles = 0;

    for (pos = 0; pos < ST_SINE_SIZE; pos++) {
        double sine_val = sin(2.0 * 3.14159265358979 * pos / ST_SINE_SIZE);
        int    top8;

        st_sine16[pos]     = (int16_t)floor(ST_AMP_TONE * sine_val + 0.5);
        st_sine16_3db[pos] = (int16_t)floor(ST_AMP_SWEEP * sine_val + 0.5);
        /* The 16-bit sample rounded to its top 8 bits. Full scale
         * rounds to 128, one past the top: kept at 127 */
        top8 = (st_sine16[pos] + 128) >> 8;
        st_sine8[pos] = (uint8_t)(128 + (top8 > 127 ? 127 : top8));
    }

    /* rate / 8, both channels, ending on a zero crossing */
    for (pos = 0; pos < ST_CLK_L; pos++) {
        double  sine_val = sin(2.0 * 3.14159265358979 * ((pos % 8) + 1) / 8.0);
        int16_t clk16    = (int16_t)floor(ST_AMP_SWEEP * sine_val + 0.5);
        uint8_t clk8     = (uint8_t)(128 + floor(ST_AMP_SWEEP / 256.0 * sine_val + 0.5));

        st_clk16[2 * pos] = st_clk16[2 * pos + 1] = clk16;
        st_clk8[2 * pos]  = st_clk8[2 * pos + 1]  = clk8;
    }

    /* At the low end the log steps are under one cycle apart, rounding
     * would repeat a frequency: each step gets one more at least */
    for (pos = 0; pos < PCMX_SWEEP_STEPS; pos++) {
        unsigned cycles = (unsigned)floor(ST_SWEEP_LOW * pow(ST_SWEEP_HIGH / ST_SWEEP_LOW,
                              (double)pos / (PCMX_SWEEP_STEPS - 1)) * ST_STEP_L / ST_RATE16 + 0.5);

        if (cycles <= prev_cycles)
            cycles = prev_cycles + 1;
        st_sweep_cycles[pos] = prev_cycles = cycles;
    }
}

void st_init(blaster_cfg_t *cfg) {
    st_stereo_ok = st16_ok = 0;
    st_sbpro = dsp_major == 3;
    hdma = (uint8_t)cfg->high_dma;

    if (dsp_major < 3) {
        printf("PCM stereo/16-bit: not played (DSP %u.xx is mono only)\n", dsp_major);
        return;
    }

    if (!st_step[0] || !st_step[1] || !st_clk16 || !st_clk8) {
        printf("PCM stereo/16-bit: not played (no memory for buffers)\n");
        return;
    }

    st_build_tables();
    st_stereo_ok = 1;

    if (dsp_major >= 4 && cfg->high_dma >= 5 && cfg->high_dma <= 7)
        st16_ok = 1;

    printf("PCM stereo: 8-bit, %s\n", st_sbpro ? "SB Pro method, 21739 Hz" : "22050 Hz");
    if (st16_ok)
        printf("PCM 16-bit: stereo, 44100 Hz, DMA %u\n", hdma);
    else
        printf("PCM 16-bit: not played (%s)\n",
               dsp_major < 4 ? "needs DSP 4.xx" : "BLASTER has no H5-H7");
}

/* Cards that can't play a kind skip it: MDF.C plays a filler */
int st_enabled(pcmx_kind_t kind) {
    return ready && (st_info[kind].section == PCMX_STEREO ? st_stereo_ok : st16_ok);
}

/* ---- DMA ---- */

/* As dma_program(), on the 16-bit controller: address and count in words */
void dma16_program(void __far *buf, uint16_t words, uint8_t mode) {
    uint32_t phys         = phys_of(buf);
    uint8_t  channel      = (uint8_t)(hdma & 3);
    uint16_t word_offset  = (uint16_t)((phys >> 1) & 0xFFFF);
    uint16_t words_minus_1 = (uint16_t)(words - 1);

    outp(DMA16_MASK, 0x04 | channel);
    outp(DMA16_FLIPFLOP, 0);
    outp(DMA16_MODE, mode | channel);
    outp(0xC0 + channel * 4, word_offset & 0xFF);
    outp(0xC0 + channel * 4, word_offset >> 8);
    outp(0xC2 + channel * 4, words_minus_1 & 0xFF);
    outp(0xC2 + channel * 4, words_minus_1 >> 8);
    outp(dma16_page_port[channel], (uint8_t)((phys >> 16) & 0xFE));
    outp(DMA16_MASK, channel);
}

void st_mask_all() {
    dma_mask();

    if (st16_ok)
        outp(DMA16_MASK, 0x04 | (hdma & 3));
}

/* ---- Play ---- */

/* DSP 4.xx takes the rate in Hz, DSP 3.xx a time constant */
int st_set_rate(int bits16) {
    unsigned rate = bits16 ? ST_RATE16 : ST_RATE8;

    if (st_sbpro)
        return dsp_write(base, DSP_TIME_CONST) && dsp_write(base, ST_TC8);

    return dsp_write(base, DSP_RATE_OUT) &&
           dsp_write(base, (uint8_t)(rate >> 8)) &&
           dsp_write(base, (uint8_t)(rate & 0xFF));
}

/* Loops a buffer of stereo frames, plays it "loops" times.
 * DMA wraps over the buffer (auto-init). DSP has the full
 * length, so it raises the IRQ at the end */
int st_play(void __far *buf, unsigned frames, unsigned loops, int bits16) {
    uint32_t total_units = 2UL * frames * loops;        /* bytes or words */
    uint16_t units_minus_1 = (uint16_t)(total_units - 1);

    if (!st_set_rate(bits16))
        return 0;

    if (bits16) {
        dma16_program(buf, (uint16_t)(2 * frames), DMA_MODE_AUTO);
        return dsp_write(base, DSP_SB16_16BIT) && dsp_write(base, MODE_S16_STEREO) &&
               dsp_write(base, (uint8_t)(units_minus_1 & 0xFF)) && dsp_write(base, (uint8_t)(units_minus_1 >> 8));
    }

    dma_program((uint8_t __far *)buf, (uint16_t)(2 * frames), DMA_MODE_AUTO);
    /* High-speed takes its length from the block size command */
    if (st_sbpro)
        return dsp_play(DSP_BLOCK_SIZE, (uint16_t)(units_minus_1 + 1)) && dsp_write(base, DSP_HS_8BIT);

    return dsp_write(base, DSP_SB16_8BIT) && dsp_write(base, MODE_U8_STEREO) &&
           dsp_write(base, (uint8_t)(units_minus_1 & 0xFF)) && dsp_write(base, (uint8_t)(units_minus_1 >> 8));
}

/* Missing IRQ. Stop and reset, the rate goes again with the next play */
void st_recover() {
    st_mask_all();
    recover();
}

/* DSP 3.xx: stereo on, output filter off and one silent byte so the
 * channels start in order. */
void pcmx_begin() {
    if (!st_enabled(PCMX_SEP8) || !st_sbpro)
        return;

    st_sbp_saved = mix_read(base, SBP_OUT_CTRL);
    mix_write(base, SBP_OUT_CTRL, (uint8_t)(st_sbp_saved | SBP_STEREO | SBP_FILTER_OFF));
    dma_program(&prime_byte, 1, DMA_MODE_SINGLE);
    dsp_play(DSP_PLAY_8BIT, 1);
}

/* ---- Steps ---- */

unsigned st_cycles(pcmx_kind_t kind, unsigned step) {
    if (kind == PCMX_SEP8)
        return ST_TONE_CYCLES8;

    if (kind == PCMX_SWEEP16)
        return st_sweep_cycles[step];

    return ST_TONE_CYCLES16;
}

/* Queues a step into the buffer not playing */
void pcmx_step_fill(pcmx_kind_t kind, unsigned step) {
    if (!st_enabled(kind) || st_info[kind].clock)
        return;

    sf.bits16 = st_info[kind].bits16;
    sf.dest = st_step[st_next];
    sf.pos = 0;
    sf.phase = 0;
    sf.phase_inc = (uint32_t)(st_cycles(kind, step) * 4294967296.0 / ST_STEP_L);
    sf.channels = 3;

    if (kind == PCMX_SEP8 || kind == PCMX_SEP16)
        sf.channels = step == 0 ? 1 : (step == 1 ? 2 : 3);  /* left, right, both */

    sf.level_shift = kind == PCMX_LEVEL16 ? step * ST_LEVEL_SHIFT : 0;
    sf.table  = kind == PCMX_SWEEP16 ? st_sine16_3db : st_sine16;
    sf.active = 1;
}

/* Up to max_frames more of the step being built. A silent channel
 * gets its silence value through a mask instead of a test per sample.
 * All ones keeps the sample, zero clears it */
void st_fill_samples(unsigned max_frames) {
    unsigned end, pos;

    if (!sf.active)
        return;

    end = sf.pos + max_frames;
    if (end > ST_STEP_L)
        end = ST_STEP_L;

    if (sf.bits16) {
        /* Signed: silence is 0, the mask alone gives it */
        int16_t __far *dest = (int16_t __far *)sf.dest + 2 * sf.pos;
        int16_t left_mask  = (sf.channels & 1) ? -1 : 0;
        int16_t right_mask = (sf.channels & 2) ? -1 : 0;

        for (pos = sf.pos; pos < end; pos++) {
            int16_t sample = (int16_t)(sf.table[(uint16_t)(sf.phase >> 16) >> (16 - ST_SINE_BITS)] >> sf.level_shift);

            dest[0] = sample & right_mask;      /* measured: first of a pair plays right */
            dest[1] = sample & left_mask;
            dest += 2;
            sf.phase += sf.phase_inc;
        }
    } else {
        /* 8-bit: Separation only, full scale.
         * Unsigned: silence is 0x80 */
        uint8_t __far *dest = sf.dest + 2 * sf.pos;
        uint8_t left_mask  = (sf.channels & 1) ? 0xFF : 0;
        uint8_t right_mask = (sf.channels & 2) ? 0xFF : 0;

        for (pos = sf.pos; pos < end; pos++) {
            uint8_t sample = st_sine8[(uint16_t)(sf.phase >> 16) >> (16 - ST_SINE_BITS)];

            dest[0] = (uint8_t)((sample & right_mask) | (0x80 & ~right_mask));
            dest[1] = (uint8_t)((sample & left_mask) | (0x80 & ~left_mask));
            dest += 2;
            sf.phase += sf.phase_inc;
        }
    }

    sf.pos = end;
    if (sf.pos >= ST_STEP_L) {
        sf.active = 0;
        st_next ^= 1;       /* the next step goes into the other buffer */
    }
}

/* Counts the IRQ of whatever played before, then starts this one on the frame edge */
void pcmx_step_start(pcmx_kind_t kind) {
    const st_kind_info_t *info = &st_info[kind];
    int started;

    if (!st_enabled(kind))
        return;

    if (st_pending >= 0) {
        if (irq_count != st_irq_base)
            st_count[st_pending].got++;
        else
            st_recover();
        st_pending = -1;
    }

    st_irq_base = irq_count;
    st_count[kind].expected++;

    if (info->clock)
        started = info->bits16 ? st_play(st_clk16, ST_CLK_L, info->loops, 1)
                               : st_play(st_clk8, ST_CLK_L, info->loops, 0);
    else {
        /* Should be built by now */
        if (sf.active)
            st_fill_samples(ST_STEP_L);
        /* Building flipped st_next when done: the built one is the other */
        started = st_play(st_step[st_next ^ 1], ST_STEP_L, info->loops, info->bits16);
    }

    if (started)
        st_pending = kind;
    else
        st_recover();
}

void pcmx_steps_end(pcmx_kind_t kind) {
    if (!st_enabled(kind) || st_pending < 0)
        return;

    if (irq_count != st_irq_base) {
        st_count[st_pending].got++;
        st_mask_all();
    } else
        st_recover();

    st_pending = -1;
}

void st_finish() {
    if (st_stereo_ok)
        st_mask_all();

    if (st_sbp_saved >= 0) {
        mix_write(base, SBP_OUT_CTRL, (uint8_t)st_sbp_saved);
        st_sbp_saved = -1;
    }
}

int st_print_report() {
    int ok = 1;

    if (st_stereo_ok) {
        printf("  PCM stereo IRQs:");
        ok &= print_count(&st_count[PCMX_CLK8]);
        ok &= print_count(&st_count[PCMX_SEP8]);
        printf("\n");
    }

    if (st16_ok) {
        printf("  PCM 16-bit IRQs:");
        ok &= print_count(&st_count[PCMX_CLK16]);
        ok &= print_count(&st_count[PCMX_SWEEP16]);
        ok &= print_count(&st_count[PCMX_LEVEL16]);
        ok &= print_count(&st_count[PCMX_SEP16]);
        printf("\n");
    }

    return ok;
}
