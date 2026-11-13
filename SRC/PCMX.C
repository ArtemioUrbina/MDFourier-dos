/*
 * 8-bit stereo and 16-bit stereo PCM, for PCM.C
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <conio.h>
#include <dos.h>
#include <i86.h>
#include "pcm.h"
#include "pcmdrv.h"
#include "pcmint.h"
#include "wss.h"
#include "sax.h"
#include "dsp.h"
#include "mixer.h"
#include "host.h"

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
 * stereo on and output filter off, one silent byte as the guide does,
 * then high-speed transfers at 1 MHz/23 bytes/s (21739 Hz per
 * channel). Mixer 0x0E restored by pcm_finish().
 * 8-bit stereo, DSP 4.xx: 0xC0 with the stereo mode bit, 22050 Hz.
 * 16-bit stereo: a pcm16_driver_t (PCMDRV.H)
 *   DSP 4.xx: 0xB0 signed stereo, 44100 Hz, high DMA, below.
 *   ESS ES18xx: Extended mode, 44111 Hz measured, 8-bit DMA (ESS.C).
 *   WSS codec: OPL3-SAx, 44100 Hz, its own DMA and IRQ (WSS.C).
 *   Crystal CS4236/CS4237B: the same WSS.C, on the SB side's IRQ and DMA.
 *
 * The rate goes with every transfer. DSPs take it right away, and
 * after a pcm_recover() it is back without anything to track.
 *
 * A frame below is one sample per channel
 * 2 bytes in 8-bit stereo.
 * 2 words in 16-bit. Left first, as the SB16 and ES1869 data sheets
 * say and as measured on every card, SB Pro mode included
 * ====================================================================== */

#define DSP_RATE_OUT    0x41    /* DSP 4.xx output rate, high byte first */
#define DSP_SB16_8BIT   0xC0    /* DSP 4.xx 8-bit single-cycle, FIFO off */
#define DSP_SB16_16BIT  0xB0    /* DSP 4.xx 16-bit single-cycle, FIFO off */
#define MODE_U8_STEREO  0x20
#define MODE_S16_STEREO 0x30

/* SB Pro stereo takes bytes at twice the per-channel rate, left and
 * right in turn. 43478 bytes/s is over the ~23 kHz of normal mode.
 * Needs the high-speed command */
#define PCMX_RATE16     44100u
#define PCMX_RATE8      22050u  /* per channel, DSP 4.xx */
#define SBPRO_STEREO_TC 233     /* DSP 3.xx: 43478 bytes/s, 21739 per channel */


/* Level and Separation tones at full scale: if a card's stereo or 16-bit
 * path is louder than its 8-bit mono one, that is the hardware and
 * MDFourier documents it. Sweep and clocks 3 dB down, as in 8-bit mono,
 * so the reference stays on a 1 kHz step and not on whichever sweep
 * frequency a card boosts */
#define PCMX_AMP_TONE     32767 /* 0 dB: Separation and the first Level step */
#define PCMX_AMP_SWEEP    23198 /* -3 dB: sweep and clocks */
#define PCMX_LEVEL_SHIFT  2     /* 12 dB per Level step */
#define PCMX_TONE_CYCLES8  243  /* 1000.6 Hz at 22050, in PCMX_STEP_FRAMES */
#define PCMX_TONE_CYCLES16 121  /*  996.4 Hz at 44100 */
#define PCMX_SWEEP_LOW    25.0
#define PCMX_SWEEP_HIGH   20000.0


/* Built once, steps only shift or look them up. No multiply per
 * sample: a 32-bit one is a library call on a 16-bit CPU */
#define PCMX_SINE_BITS  10
#define PCMX_SINE_SIZE  (1 << PCMX_SINE_BITS)
int16_t pcmx_sine16_full[PCMX_SINE_SIZE];    /* PCMX_AMP_TONE */
int16_t pcmx_sine16_sweep[PCMX_SINE_SIZE];   /* PCMX_AMP_SWEEP */
uint8_t pcmx_sine8_full[PCMX_SINE_SIZE];     /* PCMX_AMP_TONE, unsigned 8-bit */

/* Sweep cycles per loop buffer, log spaced, one more than the step
 * before at least */
unsigned pcmx_sweep16_cycles[PCMX_SWEEP_STEPS];

int      pcmx_stereo_ok;              /* 8-bit stereo plays */
char     *pcm16_failure;         /* the driver's begin() refused, why */
int      pcmx_sbpro_path;             /* DSP 3.xx */
uint8_t  pcmx_high_dma;
uint8_t  __far pcmx_sbpro_prime_byte = 0x80;

uint8_t __far *pcmx_clock8_buf;       /* PCMX_CLOCK_FRAMES stereo 8-bit frames */
int16_t __far *pcmx_clock16_buf;      /* PCMX_CLOCK_FRAMES stereo 16-bit frames */
uint8_t __far *pcmx_step_buf[2]; /* PCMX_STEP_FRAMES stereo 16-bit frames each */
unsigned pcmx_fill_buf;          /* buffer the next step goes into */

typedef struct {
    pcmx_section_t section;
    int            bits16;
    int            clock;               /* plays its own clock buffer */
    unsigned       loops;               /* buffers played per start */
} pcmx_kind_info_t;

/* Clocks: 4 loops of 4096 frames at 22050 Hz, 8 at 44100, both ~0.74 s
 * of the 0.86 s block. 8 loops of 16-bit stereo are 65536 words, the
 * most one DSP count can hold */
pcmx_kind_info_t pcmx_kind_info[PCMX_KIND_COUNT] = {
    { PCMX_STEREO, 0, 1, 4 },           /* PCMX_CLK8,   ~0.74 s */
    { PCMX_STEREO, 0, 0, 1 },           /* PCMX_SEP8 */
    { PCMX_16,     1, 1, 8 },           /* PCMX_CLK16,  ~0.74 s */
    { PCMX_16,     1, 0, 1 },           /* PCMX_SWEEP16 */
    { PCMX_16,     1, 0, 2 },           /* PCMX_LEVEL16 */
    { PCMX_16,     1, 0, 2 },           /* PCMX_SEP16 */
};

pcm_count_t pcmx_counts[PCMX_KIND_COUNT] = {
    { "clock", 0, 0 }, { "separation", 0, 0 },
    { "clock", 0, 0 }, { "sweep", 0, 0 }, { "level", 0, 0 }, { "separation", 0, 0 }
};

unsigned pcmx_irq_base;              /* pcm_irq_count when the tone started */
unsigned pcmx_irqs_expected = 1;     /* IRQs the tone raises */
int      pcmx_playing_kind  = -1;    /* kind playing, its IRQs not counted yet */

/* The step being built, a chunk per frame. Same phase accumulator as
 * fill, over the loop buffer: phase_inc = cycles / PCMX_STEP_FRAMES *
 * 2^32, so the buffer holds exactly that many cycles */
struct {
    int             active;
    int             bits16;
    uint8_t __far   *dest;
    int16_t         *table;
    unsigned        pos;
    unsigned        channels;           /* bit 0 left, bit 1 right */
    unsigned        level_shift;        /* 6 dB per bit */
    uint32_t        phase, phase_inc;
} pcmx_fill;

void pcmx_build_tables() {
    unsigned pos, prev_cycles = 0;

    for (pos = 0; pos < PCMX_SINE_SIZE; pos++) {
        double sine_val = sin(2.0 * 3.14159265358979 * pos / PCMX_SINE_SIZE);
        int    top8;

        pcmx_sine16_full[pos]  = (int16_t)floor(PCMX_AMP_TONE * sine_val + 0.5);
        pcmx_sine16_sweep[pos] = (int16_t)floor(PCMX_AMP_SWEEP * sine_val + 0.5);
        /* The 16-bit sample rounded to its top 8 bits. Full scale
         * rounds to 128, one past the top: kept at 127 */
        top8 = (pcmx_sine16_full[pos] + 128) >> 8;
        pcmx_sine8_full[pos] = (uint8_t)(128 + (top8 > 127 ? 127 : top8));
    }

    /* rate / 8, both channels, ending on a zero crossing */
    for (pos = 0; pos < PCMX_CLOCK_FRAMES; pos++) {
        double  sine_val = sin(2.0 * 3.14159265358979 * ((pos % 8) + 1) / 8.0);
        int16_t clk16    = (int16_t)floor(PCMX_AMP_SWEEP * sine_val + 0.5);
        uint8_t clk8     = (uint8_t)(128 + floor(PCMX_AMP_SWEEP / 256.0 * sine_val + 0.5));

        pcmx_clock16_buf[2 * pos] = pcmx_clock16_buf[2 * pos + 1] = clk16;
        pcmx_clock8_buf[2 * pos]  = pcmx_clock8_buf[2 * pos + 1]  = clk8;
    }

    /* At the low end the log steps are under one cycle apart, rounding
     * would repeat a frequency: each step gets one more at least */
    for (pos = 0; pos < PCMX_SWEEP_STEPS; pos++) {
        unsigned cycles = (unsigned)floor(PCMX_SWEEP_LOW * pow(PCMX_SWEEP_HIGH / PCMX_SWEEP_LOW,
                              (double)pos / (PCMX_SWEEP_STEPS - 1)) * PCMX_STEP_FRAMES / PCMX_RATE16 + 0.5);

        if (cycles <= prev_cycles)
            cycles = prev_cycles + 1;
        pcmx_sweep16_cycles[pos] = prev_cycles = cycles;
    }
}

/* ---- SB16 16-bit driver ---- */

int pcmx_set_rate(unsigned rate);

/* As dma_program(), on the 16-bit controller: address and count in words */
/* A command and its length - 1, as the DSP 4.xx transfers take it */
int sb16_dsp_play(uint8_t cmd, uint8_t mode, uint16_t units_minus_1) {
    return dsp_write(sb_base, cmd) && dsp_write(sb_base, mode) &&
           dsp_write(sb_base, (uint8_t)(units_minus_1 & 0xFF)) &&
           dsp_write(sb_base, (uint8_t)(units_minus_1 >> 8));
}

void sb16_print_info() {
    printf(" PCM 16-bit: stereo, 44100 Hz, DMA %u\n", pcmx_high_dma);
}

/* DMA wraps over the buffer (auto-init). DSP has the full length, so
 * it raises the IRQ at the end */
unsigned sb16_play(void __far *buf, unsigned frames, unsigned loops) {
    uint16_t words_minus_1 = (uint16_t)(2UL * frames * loops - 1);

    if (!pcmx_set_rate(PCMX_RATE16))
        return 0;
    dma16_program(pcmx_high_dma, buf, (uint16_t)(2 * frames), DMA_MODE_AUTO);
    return sb16_dsp_play(DSP_SB16_16BIT, MODE_S16_STEREO, words_minus_1);
}

void sb16_stop() {
    outp(DMA16_MASK, 0x04 | (pcmx_high_dma & 3));
}

pcm16_driver_t sb16_driver = {
    "",
    sb16_print_info,
    NULL,               /* begin */
    sb16_play,
    NULL,               /* poll */
    sb16_stop,
    NULL,               /* after_reset */
    NULL,               /* end */
    NULL                /* shared_isr */
};

/* ---- Setup ---- */

/* What plays, and why the rest doesn't. The reason goes on the next
 * line when both don't fit in 79 columns: a full 80 wraps on its own */
void pcmx_print_plays(char *plays, char *reason) {
    printf(" PCM: %s", plays);
    if (6 + strlen(plays) + 3 + strlen(reason) > 79)
        printf("\n      (%s)\n", reason);
    else
        printf(" (%s)\n", reason);
}

void pcmx_init(blaster_cfg_t *cfg) {
    char reason[32];

    pcmx_stereo_ok  = 0;
    pcm16      = NULL;
    pcmx_sbpro_path = pcm_dsp_major == 3;
    pcmx_high_dma   = (uint8_t)cfg->high_dma;

    /* High-speed mode came with DSP 2.01 */
    if (pcm_dsp_major < 3 && pcm_hs_ok) {
        sprintf(reason, "DSP %u.xx is mono only", pcm_dsp_major);
        pcmx_print_plays("8-bit mono, high-speed; stereo and 16-bit not played", reason);
        return;
    }
    if (pcm_dsp_major < 3) {
        sprintf(reason, "DSP %u.%02u", pcm_dsp_major, pcm_dsp_minor);
        pcmx_print_plays("8-bit mono; high-speed, stereo and 16-bit not played", reason);
        return;
    }

    if (!pcmx_step_buf[0] || !pcmx_step_buf[1] || !pcmx_clock16_buf || !pcmx_clock8_buf) {
        pcmx_print_plays("8-bit mono, high-speed; stereo and 16-bit not played", "no memory for buffers");
        return;
    }

    pcmx_build_tables();
    pcmx_stereo_ok = 1;

    if (pcm_dsp_major >= 4 && cfg->high_dma >= 5 && cfg->high_dma <= 7)
        pcm16 = &sb16_driver;
    /* DSP 3.01 with the ESS ID: Extended mode, no high DMA
     * Windows owns the hardware behind the SB interface */
    else if (pcmx_sbpro_path && host_is_windows())
        pcm16 = NULL;
    else if (pcmx_sbpro_path && mixer_is_ess(sb_base))
        pcm16 = &ess_driver;
    /* Other SB Pro compatibles: a WSS codec next to it (OPL3-SAx), SAX.C */
    else if (pcmx_sbpro_path && wss_in_use())
        pcm16 = &wss_driver;

    if (verbose) {
        printf(" PCM stereo: 8-bit, %s\n", pcmx_sbpro_path ? "SB Pro method, 21739 Hz" : "22050 Hz");
        if (pcm16)
            pcm16->print_info();
    }

    /* One line of what plays, the reason when 16-bit doesn't */
    if (pcm16) {
        if (!verbose)
            printf(" PCM: 8-bit mono, high-speed, 8-bit stereo, 16-bit stereo%s\n", pcm16->suffix);
    } else
        pcmx_print_plays("8-bit mono, high-speed, 8-bit stereo, 16-bit not played",
                         pcm_dsp_major >= 4 ? "BLASTER has no H5-H7" :
                         host_is_windows() ? "under Windows only through a DSP 4.xx" :
                         sax_no_16bit_reason() ? sax_no_16bit_reason() : "needs DSP 4.xx");
}

/* Cards that can't play a kind skip it: MDF.C plays a filler */
int pcmx_enabled(pcmx_kind_t kind) {
    return pcm_ready && (pcmx_kind_info[kind].section == PCMX_STEREO ? pcmx_stereo_ok : pcm16 != NULL);
}

/* DSP 3.xx: stereo on, output filter off and one silent byte first, the
 * documented SB Pro procedure. The Orpheus II's CS4237B plays the same
 * with or without it */
void pcmx_begin() {
    if (!pcmx_enabled(PCMX_SEP8) || !pcmx_sbpro_path)
        return;

    mixer_sbpro_stereo_on(sb_base);
    dma_program(sb_dma, &pcmx_sbpro_prime_byte, 1, DMA_MODE_SINGLE);
    pcm_dsp_play(DSP_PLAY_8BIT, 1);
}

/* Right after the 8-bit stereo section, before the 16-bit one's gap.
 * SB Pro mixer 0x0E back as it was, then the driver's own setup. A
 * card that doesn't take it plays the filler as one without 16-bit */
void pcmx_begin16() {
    if (!pcm_ready || !pcm16 || !pcm16->begin)
        return;

    mixer_sbpro_stereo_off();
    pcm16_failure = pcm16->begin();
    if (pcm16_failure)
        pcm16 = NULL;
}

/* ---- Play ---- */

/* DSP 4.xx takes the rate in Hz, DSP 3.xx a time constant */
int pcmx_set_rate(unsigned rate) {
    if (pcmx_sbpro_path)
        return dsp_write(sb_base, DSP_TIME_CONST) && dsp_write(sb_base, SBPRO_STEREO_TC);

    return dsp_write(sb_base, DSP_RATE_OUT) &&
           dsp_write(sb_base, (uint8_t)(rate >> 8)) &&
           dsp_write(sb_base, (uint8_t)(rate & 0xFF));
}

/* Loops a buffer of stereo frames, plays it "loops" times. The IRQs it
 * raises, 0 if it didn't start */
unsigned pcmx_play(void __far *buf, unsigned frames, unsigned loops, int bits16) {
    uint16_t bytes_minus_1 = (uint16_t)(2UL * frames * loops - 1);

    if (bits16)
        return pcm16->play(buf, frames, loops);

    if (!pcmx_set_rate(PCMX_RATE8))
        return 0;

    dma_program(sb_dma, buf, (uint16_t)(2 * frames), DMA_MODE_AUTO);
    /* High-speed takes its length from the block size command */
    if (pcmx_sbpro_path)
        return pcm_dsp_play(DSP_BLOCK_SIZE, (uint16_t)(bytes_minus_1 + 1)) && dsp_write(sb_base, DSP_HS_8BIT);

    return sb16_dsp_play(DSP_SB16_8BIT, MODE_U8_STEREO, bytes_minus_1);
}

/* Every DMA channel a stereo or 16-bit tone may be on */
void pcmx_stop() {
    dma_mask(sb_dma);
    if (pcm16 && pcm16->stop)
        pcm16->stop();
}

/* Missing IRQ. Stop and reset, the rate goes again with the next play */
void pcmx_recover() {
    pcmx_stop();
    pcm_recover();
    if (pcm16 && pcm16->after_reset)
        pcm16->after_reset();
}

/* Once per frame: the driver sees its tone's IRQs */
void pcmx_poll() {
    if (pcm16 && pcm16->poll && pcmx_playing_kind >= 0)
        pcm16->poll(pcm_irq_count - pcmx_irq_base);
}

/* ---- Steps ---- */

unsigned pcmx_cycles(pcmx_kind_t kind, unsigned step) {
    if (kind == PCMX_SEP8)
        return PCMX_TONE_CYCLES8;

    if (kind == PCMX_SWEEP16)
        return pcmx_sweep16_cycles[step];

    return PCMX_TONE_CYCLES16;
}

/* Queues a step into the buffer not playing */
void pcmx_step_fill(pcmx_kind_t kind, unsigned step) {
    if (!pcmx_enabled(kind) || pcmx_kind_info[kind].clock)
        return;

    pcmx_fill.bits16    = pcmx_kind_info[kind].bits16;
    pcmx_fill.dest      = pcmx_step_buf[pcmx_fill_buf];
    pcmx_fill.pos       = 0;
    pcmx_fill.phase     = 0;
    pcmx_fill.phase_inc = (uint32_t)(pcmx_cycles(kind, step) * 4294967296.0 / PCMX_STEP_FRAMES);
    pcmx_fill.channels  = 3;

    if (kind == PCMX_SEP8 || kind == PCMX_SEP16)
        pcmx_fill.channels = step == 0 ? 1 : (step == 1 ? 2 : 3);  /* left, right, both */

    pcmx_fill.level_shift = kind == PCMX_LEVEL16 ? step * PCMX_LEVEL_SHIFT : 0;
    pcmx_fill.table       = kind == PCMX_SWEEP16 ? pcmx_sine16_sweep : pcmx_sine16_full;
    pcmx_fill.active      = 1;
}

/* Up to max_frames more of the step being built. A silent channel
 * gets its silence value through a mask instead of a test per sample.
 * All ones keeps the sample, zero clears it */
void pcmx_fill_samples(unsigned max_frames) {
    unsigned end, pos;

    if (!pcmx_fill.active)
        return;

    end = pcmx_fill.pos + max_frames;
    if (end > PCMX_STEP_FRAMES)
        end = PCMX_STEP_FRAMES;

    if (pcmx_fill.bits16) {
        /* Signed: silence is 0, the mask alone gives it */
        int16_t __far *dest = (int16_t __far *)pcmx_fill.dest + 2 * pcmx_fill.pos;
        int16_t left_mask  = (pcmx_fill.channels & 1) ? -1 : 0;
        int16_t right_mask = (pcmx_fill.channels & 2) ? -1 : 0;

        for (pos = pcmx_fill.pos; pos < end; pos++) {
            int16_t sample = (int16_t)(pcmx_fill.table[(uint16_t)(pcmx_fill.phase >> 16) >> (16 - PCMX_SINE_BITS)]
                                       >> pcmx_fill.level_shift);

            dest[0] = sample & left_mask;
            dest[1] = sample & right_mask;
            dest += 2;
            pcmx_fill.phase += pcmx_fill.phase_inc;
        }
    } else {
        /* 8-bit: Separation only, full scale.
         * Unsigned: silence is 0x80 */
        uint8_t __far *dest = pcmx_fill.dest + 2 * pcmx_fill.pos;
        uint8_t left_mask  = (pcmx_fill.channels & 1) ? 0xFF : 0;
        uint8_t right_mask = (pcmx_fill.channels & 2) ? 0xFF : 0;

        for (pos = pcmx_fill.pos; pos < end; pos++) {
            uint8_t sample = pcmx_sine8_full[(uint16_t)(pcmx_fill.phase >> 16) >> (16 - PCMX_SINE_BITS)];

            dest[0] = (uint8_t)((sample & left_mask) | (0x80 & ~left_mask));
            dest[1] = (uint8_t)((sample & right_mask) | (0x80 & ~right_mask));
            dest += 2;
            pcmx_fill.phase += pcmx_fill.phase_inc;
        }
    }

    pcmx_fill.pos = end;
    if (pcmx_fill.pos >= PCMX_STEP_FRAMES) {
        pcmx_fill.active = 0;
        pcmx_fill_buf ^= 1;     /* the next step goes into the other buffer */
    }
}

/* Counts the IRQ of whatever played before, then starts this one on the frame edge */
void pcmx_step_start(pcmx_kind_t kind) {
    pcmx_kind_info_t *info = &pcmx_kind_info[kind];
    unsigned irqs;

    if (!pcmx_enabled(kind))
        return;

    if (pcmx_playing_kind >= 0) {
        if (pcm_irq_count - pcmx_irq_base >= pcmx_irqs_expected)
            pcmx_counts[pcmx_playing_kind].got++;
        else
            pcmx_recover();
        pcmx_playing_kind = -1;
    }

    pcmx_irq_base = pcm_irq_count;
    pcmx_counts[kind].expected++;

    if (info->clock)
        irqs = info->bits16 ? pcmx_play(pcmx_clock16_buf, PCMX_CLOCK_FRAMES, info->loops, 1)
                            : pcmx_play(pcmx_clock8_buf, PCMX_CLOCK_FRAMES, info->loops, 0);
    else {
        /* Should be built by now */
        if (pcmx_fill.active)
            pcmx_fill_samples(PCMX_STEP_FRAMES);
        /* Building flipped pcmx_fill_buf when done: the built one is the other */
        irqs = pcmx_play(pcmx_step_buf[pcmx_fill_buf ^ 1], PCMX_STEP_FRAMES, info->loops, info->bits16);
    }

    if (irqs) {
        pcmx_irqs_expected = irqs;
        pcmx_playing_kind  = kind;
    } else
        pcmx_recover();
}

void pcmx_steps_end(pcmx_kind_t kind) {
    if (!pcmx_enabled(kind) || pcmx_playing_kind < 0)
        return;

    if (pcm_irq_count - pcmx_irq_base >= pcmx_irqs_expected) {
        pcmx_counts[pcmx_playing_kind].got++;
        pcmx_stop();
    } else
        pcmx_recover();

    pcmx_playing_kind = -1;
}

void pcmx_finish() {
    if (pcmx_stereo_ok)
        pcmx_stop();

    if (pcm16 && pcm16->end)
        pcm16->end();

    mixer_sbpro_stereo_off();
}

int pcmx_print_report() {
    int ok = 1;

    if (pcmx_stereo_ok) {
        printf("  PCM stereo IRQs:");
        ok &= pcm_print_count(&pcmx_counts[PCMX_CLK8]);
        ok &= pcm_print_count(&pcmx_counts[PCMX_SEP8]);
        printf("\n");
    }

    if (pcm16) {
        printf("  PCM 16-bit IRQs:");
        ok &= pcm_print_count(&pcmx_counts[PCMX_CLK16]);
        ok &= pcm_print_count(&pcmx_counts[PCMX_SWEEP16]);
        ok &= pcm_print_count(&pcmx_counts[PCMX_LEVEL16]);
        ok &= pcm_print_count(&pcmx_counts[PCMX_SEP16]);
        printf("\n");
    }
    if (pcm16_failure) {
        printf("  WARNING: %s", pcm16_failure);
        ok = 0;
    }

    return ok;
}
