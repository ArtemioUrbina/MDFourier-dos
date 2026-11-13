/*
 * Creative ADPCM through the Sound Blaster DSP, for PCM.C
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
#include "dsp.h"
#include "pit.h"

/* ======================================================================
 * ADPCM
 *
 * Creative's 4, 2.6 and 2-bit ADPCM: the DSP decodes each code into an
 * 8-bit sample, adding a step to the previous sample and adapting the
 * step size from the code. Single-cycle, every transfer starts with a
 * reference byte (the first sample) and the smallest step size. As in
 * Creative's programming guide, every DSP from 1.xx to 4.xx takes the
 * commands, many clones don't: pcm_adpcm_probe() tries one first.
 *
 * Each tone is encoded against the decoder itself: for every sample we
 * pick the code whose decoded result lands closest to the sine, so the
 * encoder follows what the card will play. The decoder tables are the
 * ones DOSBox-X uses for the Sound Blaster, from real hardware.
 *
 * One rate for all three, 10000 Hz (TC 156): in range for 4-bit (12 kHz),
 * 2.6-bit (13 kHz) and 2-bit (11 kHz) on DSP 1.xx/2.xx. Measured: the
 * SB 2.0 plays it at 10001 Hz, the SB16 at 9958 Hz, unlike 8-bit PCM at
 * the same card's time constant 211. The clock tone measures it.
 * ====================================================================== */

#define ADPCM_TC        156
#define ADPCM_RATE      (1000000.0 / 100.0)

/* Sweep from 50 Hz to 4 kHz, below the 5 kHz Nyquist. Level at rate
 * / 10, 1000 Hz, the same 10 samples every period */
#define ADPCM_SWEEP_LOW     50.0
#define ADPCM_SWEEP_HIGH    4000.0
#define ADPCM_LEVEL_FREQ    (ADPCM_RATE / 10.0)

/* Single-cycle with a reference byte: the transfer's first byte is the
 * first sample, the step size starts at its smallest */
#define DSP_ADPCM4_REF  0x75
#define DSP_ADPCM3_REF  0x77
#define DSP_ADPCM2_REF  0x17

/* Step added for each code, rows of codes for each step size. The first
 * half of a row adds, the second subtracts */
int8_t pcm_adpcm4_delta[64] = {
    0,  1,  2,  3,  4,  5,  6,  7,  0,  -1,  -2,  -3,  -4,  -5,  -6,  -7,
    1,  3,  5,  7,  9, 11, 13, 15, -1,  -3,  -5,  -7,  -9, -11, -13, -15,
    2,  6, 10, 14, 18, 22, 26, 30, -2,  -6, -10, -14, -18, -22, -26, -30,
    4, 12, 20, 28, 36, 44, 52, 60, -4, -12, -20, -28, -36, -44, -52, -60
};
/* Moves to the next row (larger steps) or back, by a row's size */
int8_t pcm_adpcm4_adjust[64] = {
      0, 0, 0, 0, 0, 16, 16, 16,   0, 0, 0, 0, 0, 16, 16, 16,
    -16, 0, 0, 0, 0, 16, 16, 16, -16, 0, 0, 0, 0, 16, 16, 16,
    -16, 0, 0, 0, 0, 16, 16, 16, -16, 0, 0, 0, 0, 16, 16, 16,
    -16, 0, 0, 0, 0,  0,  0,  0, -16, 0, 0, 0, 0,  0,  0,  0
};
int8_t pcm_adpcm3_delta[40] = {
    0,  1,  2,  3,  0,  -1,  -2,  -3,
    1,  3,  5,  7, -1,  -3,  -5,  -7,
    2,  6, 10, 14, -2,  -6, -10, -14,
    4, 12, 20, 28, -4, -12, -20, -28,
    5, 15, 25, 35, -5, -15, -25, -35
};
int8_t pcm_adpcm3_adjust[40] = {
     0, 0, 0, 8,  0, 0, 0, 8,
    -8, 0, 0, 8, -8, 0, 0, 8,
    -8, 0, 0, 8, -8, 0, 0, 8,
    -8, 0, 0, 8, -8, 0, 0, 8,
    -8, 0, 0, 0, -8, 0, 0, 0
};
int8_t pcm_adpcm2_delta[24] = {
    0,  1,  0,  -1,  1,  3,  -1,  -3,
    2,  6, -2,  -6,  4, 12,  -4, -12,
    8, 24, -8, -24, 16, 48, -16, -48
};
int8_t pcm_adpcm2_adjust[24] = {
     0, 4,  0, 4,
    -4, 4, -4, 4, -4, 4, -4, 4,
    -4, 4, -4, 4, -4, 4, -4, 4,
    -4, 0, -4, 0
};

typedef struct {
    int8_t   *delta;
    int8_t   *adjust;
    uint8_t  codes;         /* per row, a power of two */
    uint8_t  rows;          /* step sizes */
    uint8_t  per_byte;      /* samples in a byte */
    uint8_t  command;
    uint8_t  pick_row;      /* its first row in pcm_adpcm_pick */
} pcm_adpcm_format_t;

pcm_adpcm_format_t pcm_adpcm_formats[3] = {
    { pcm_adpcm4_delta, pcm_adpcm4_adjust, 16, 4, 2, DSP_ADPCM4_REF, 0 },     /* PCM_ADPCM4 */
    { pcm_adpcm3_delta, pcm_adpcm3_adjust,  8, 5, 3, DSP_ADPCM3_REF, 4 },     /* PCM_ADPCM3 */
    { pcm_adpcm2_delta, pcm_adpcm2_adjust,  4, 6, 4, DSP_ADPCM2_REF, 9 }      /* PCM_ADPCM2 */
};

/* For each row and each distance to the target, 0-255, the step in
 * the row's first half that lands closest: one lookup per sample
 * instead of trying every code. Rows of the three formats, then the
 * 2.6-bit rows again for its third sample, which can only use the
 * even steps */
#define ADPCM_PICK_ROWS     (4 + 5 + 6 + 5)
#define ADPCM_EVEN_ROW      15
uint8_t pcm_adpcm_pick[ADPCM_PICK_ROWS][256];

int pcm_adpcm_ok;           /* pcm_adpcm_probe() saw the DSP take ADPCM */

void pcm_adpcm_pick_row(uint8_t *pick, int8_t *steps, uint8_t half, int even_only) {
    unsigned distance;
    uint8_t  step;

    for (distance = 0; distance < 256; distance++) {
        int best_error = 0x7FFF;

        for (step = 0; step < half; step++) {
            int error = steps[step] - (int)distance;

            if (even_only && (step & 1))
                continue;
            if (error < 0)
                error = -error;
            if (error < best_error) {
                best_error = error;
                pick[distance] = step;
            }
        }
    }
}

void pcm_adpcm_build_picks() {
    unsigned format, row;

    for (format = 0; format < 3; format++) {
        pcm_adpcm_format_t *fmt = &pcm_adpcm_formats[format];

        for (row = 0; row < fmt->rows; row++)
            pcm_adpcm_pick_row(pcm_adpcm_pick[fmt->pick_row + row],
                               fmt->delta + row * fmt->codes, (uint8_t)(fmt->codes / 2), 0);
    }
    for (row = 0; row < 5; row++)
        pcm_adpcm_pick_row(pcm_adpcm_pick[ADPCM_EVEN_ROW + row], pcm_adpcm3_delta + row * 8, 4, 1);
}

/* ---- Encoder ---- */

/* The decoder's state, followed sample by sample */
typedef struct {
    pcm_adpcm_format_t *format;
    int                 sample;     /* last decoded, 0-255 */
    int                 row;        /* offset of the step size row */
} pcm_adpcm_state_t;

/* The code whose decoded sample lands closest to target, the decoder
 * moved past it. The first half of a row moves up, the second down by
 * the same steps. even: the third 2.6-bit sample of a byte has only
 * two bits, its lowest bit is always 0 */
uint8_t pcm_adpcm_encode(pcm_adpcm_state_t *state, int target, int even) {
    pcm_adpcm_format_t *format = state->format;
    int      distance = target - state->sample;
    uint8_t  row = (uint8_t)(state->row / format->codes);
    uint8_t  best;

    row = (uint8_t)(even ? ADPCM_EVEN_ROW + row : format->pick_row + row);
    if (distance < 0)
        best = (uint8_t)(format->codes / 2 + pcm_adpcm_pick[row][-distance]);
    else
        best = pcm_adpcm_pick[row][distance];

    state->sample += format->delta[state->row + best];
    if (state->sample < 0)
        state->sample = 0;
    else if (state->sample > 255)
        state->sample = 255;
    state->row += format->adjust[state->row + best];
    return best;
}

/* The tone being encoded, a chunk per frame, like the 8-bit one. A
 * clock uses a table of one period instead of the phase accumulator */
struct {
    int                 active;
    unsigned            buf;            /* step buffer, or -1 for the clock */
    uint8_t __far       *dest;
    int8_t              *table;
    unsigned            samples, pos;   /* in samples, a whole number of bytes */
    uint32_t            phase, phase_inc;
    int                 fade;
    pcm_adpcm_state_t   state;
} pcm_adpcm_fill;

/* The clock's period: 8 samples starting at 45 degrees, the last one
 * on 0, as the 8-bit clock */
int8_t pcm_adpcm_clock_period[CLK_PERIOD];

int pcm_adpcm_target(unsigned pos) {
    int sample;

    if (!pcm_adpcm_fill.table)
        return 128 + pcm_adpcm_clock_period[pos % CLK_PERIOD];

    sample = pcm_adpcm_fill.table[(uint16_t)(pcm_adpcm_fill.phase >> 16) >> (16 - SINE_BITS)];
    pcm_adpcm_fill.phase += pcm_adpcm_fill.phase_inc;
    if (pcm_adpcm_fill.fade) {
        if (pos < FADE)
            sample = sample * (int)pos / FADE;
        else if (pos >= pcm_adpcm_fill.samples - FADE)
            sample = sample * (int)(pcm_adpcm_fill.samples - 1 - pos) / FADE;
    }
    return 128 + sample;
}

/* Whole bytes: 4-bit high nibble first, 2.6-bit 3 + 3 + 2 bits, 2-bit
 * from the top two bits down */
void pcm_adpcm_fill_samples(unsigned max_samples) {
    pcm_adpcm_state_t *state = &pcm_adpcm_fill.state;
    uint8_t  per_byte;
    unsigned end;

    if (!pcm_adpcm_fill.active)
        return;

    per_byte = state->format->per_byte;
    end = pcm_adpcm_fill.pos + max_samples;
    if (end > pcm_adpcm_fill.samples)
        end = pcm_adpcm_fill.samples;

    while (pcm_adpcm_fill.pos < end) {
        unsigned pos = pcm_adpcm_fill.pos;
        uint8_t  byte = 0, i;

        for (i = 0; i < per_byte; i++) {
            uint8_t code = pcm_adpcm_encode(state, pcm_adpcm_target(pos + i), per_byte == 3 && i == 2);

            if (per_byte == 2)
                byte = (uint8_t)((byte << 4) | code);
            else if (per_byte == 3)
                byte = (uint8_t)(i < 2 ? (byte << 3) | code : (byte << 2) | (code >> 1));
            else
                byte = (uint8_t)((byte << 2) | code);
        }
        /* After the reference byte */
        pcm_adpcm_fill.dest[1 + pos / per_byte] = byte;
        pcm_adpcm_fill.pos += per_byte;
    }
    if (pcm_adpcm_fill.pos >= pcm_adpcm_fill.samples)
        pcm_adpcm_fill.active = 0;
}

/* Starts encoding samples into dest, from a reference of 128. Returns
 * the transfer's length in bytes, the reference byte included */
uint16_t pcm_adpcm_build(pcm_adpcm_t format, uint8_t __far *dest, unsigned long samples) {
    pcm_adpcm_format_t *fmt = &pcm_adpcm_formats[format];

    samples -= samples % fmt->per_byte;
    pcm_adpcm_fill.dest    = dest;
    pcm_adpcm_fill.samples = (unsigned)samples;
    pcm_adpcm_fill.pos     = 0;
    pcm_adpcm_fill.phase   = 0;
    pcm_adpcm_fill.state.format = fmt;
    pcm_adpcm_fill.state.sample = 128;
    pcm_adpcm_fill.state.row    = 0;
    pcm_adpcm_fill.active  = 1;
    dest[0] = 128;
    return (uint16_t)(1 + samples / fmt->per_byte);
}

/* ---- Probe ----
 * The 4-bit command with a length whose two bytes, 0x60, are no
 * command on any DSP we know of: a DSP that doesn't take ADPCM reads
 * them as unknown commands and ignores them. One that does starts
 * asking for bytes, which the DMA count shows. The transfer is longer
 * than the buffer, so it never ends by itself: a reset stops it */
#define ADPCM_PROBE_BYTES   64
#define ADPCM_PROBE_LENGTH  0x6061      /* sent as 0x60, 0x60 */
#define ADPCM_PROBE_WAIT    PIT_US_TO_CLK(30000UL)

int pcm_adpcm_probe() {
    uint32_t start;
    unsigned pos;
    int      taken = 0;

    pcm_adpcm_ok = 0;
    if (!pcm_ready)
        return 0;

    /* Reference at the midpoint, then codes 0: no change, silent */
    pcm_step_buf[0][0] = 128;
    for (pos = 1; pos < ADPCM_PROBE_BYTES; pos++)
        pcm_step_buf[0][pos] = 0;

    dsp_write(sb_base, DSP_TIME_CONST);
    dsp_write(sb_base, ADPCM_TC);
    dma_program(sb_dma, pcm_step_buf[0], ADPCM_PROBE_BYTES, DMA_MODE_SINGLE);
    pcm_dsp_play(DSP_ADPCM4_REF, ADPCM_PROBE_LENGTH);

    start = pit_now();
    while (!taken && (uint32_t)(pit_now() - start) < ADPCM_PROBE_WAIT)
        taken = dma_count_left(sb_dma) != ADPCM_PROBE_BYTES - 1;

    dma_mask(sb_dma);
    dsp_reset(sb_base);
    pcm_adpcm_ok = taken;
    if (taken)
        pcm_adpcm_build_picks();
    return taken;
}

/* ---- Clock: 4-bit at rate / 8 ---- */

/* At the start of the silence before it. Built into the 8-bit clock's
 * buffer, done with by then */
void pcm_adpcm_clock_build(unsigned frames) {
    unsigned long samples;
    unsigned pos;

    if (!pcm_ready || !pcm_adpcm_ok)
        return;

    for (pos = 0; pos < CLK_PERIOD; pos++)
        pcm_adpcm_clock_period[pos] = (int8_t)floor(CLK_AMP *
                                       sin(2.0 * 3.14159265358979 * (pos + 1) / CLK_PERIOD) + 0.5);

    /* Whole periods, it ends on the midpoint */
    samples = (unsigned long)(ADPCM_RATE * frames * CLK_PCT / 100.0 / pcm_frame_rate);
    samples -= samples % (CLK_PERIOD * 2UL);
    if (samples > (CLK_MAX - 1) * 2UL)
        samples = (CLK_MAX - 1) * 2UL;

    pcm_adpcm_fill.buf   = (unsigned)-1;
    pcm_adpcm_fill.table = NULL;
    pcm_adpcm_fill.fade  = 0;
    pcm_clock_len = pcm_adpcm_build(PCM_ADPCM4, pcm_clock_buf, samples);
}

/* In the silence's last frame */
void pcm_adpcm_clock_arm() {
    if (!pcm_ready || !pcm_adpcm_ok)
        return;

    if (pcm_adpcm_fill.active)
        pcm_adpcm_fill_samples(0xFFFF);
    pcm_count_adpcm_clock.expected = 1;
    pcm_clock_count = &pcm_count_adpcm_clock;

    pcm_tc = ADPCM_TC;
    dsp_write(sb_base, DSP_TIME_CONST);
    dsp_write(sb_base, ADPCM_TC);
    dma_program(sb_dma, pcm_clock_buf, pcm_clock_len, DMA_MODE_SINGLE);
}

void pcm_adpcm_clock_start() {
    if (!pcm_ready || !pcm_adpcm_ok)
        return;

    pcm_clock_playing = 1;
    pcm_irq_count = 0;
    if (pcm_dsp_play(DSP_ADPCM4_REF, pcm_clock_len))
        pcm_dsp_busy = 1;
}

void pcm_adpcm_clock_stop() {
    if (!pcm_ready || !pcm_adpcm_ok)
        return;
    pcm_clock_stop();
}

/* ---- Steps ---- */

double pcm_adpcm_freq(pcm_steps_t kind, unsigned step) {
    if (kind == PCM_LEVEL)
        return ADPCM_LEVEL_FREQ;
    return ADPCM_SWEEP_LOW * pow(ADPCM_SWEEP_HIGH / ADPCM_SWEEP_LOW,
                                 (double)step / (PCM_ADPCM_SWEEP_STEPS - 1));
}

void pcm_adpcm_step_fill(pcm_adpcm_t format, pcm_steps_t kind, unsigned step, unsigned buf, unsigned frames) {
    unsigned long samples;

    if (!pcm_ready || !pcm_adpcm_ok)
        return;

    samples = (unsigned long)(ADPCM_RATE * frames * FILL_PCT / 100.0 / pcm_frame_rate);

    pcm_adpcm_fill.buf       = buf;
    pcm_adpcm_fill.table     = pcm_amp_sine[kind == PCM_LEVEL ? step : AMP_SWEEP_TAB];
    pcm_adpcm_fill.phase_inc = (uint32_t)(pcm_adpcm_freq(kind, step) * 4294967296.0 / ADPCM_RATE);
    pcm_adpcm_fill.fade      = 1;
    pcm_step_len[buf] = pcm_adpcm_build(format, pcm_step_buf[buf], samples);
}

/* As pcm_step_start(): the previous step's IRQ counted, this one
 * started as one single-cycle transfer */
void pcm_adpcm_step_start(pcm_adpcm_t format, pcm_steps_t kind, unsigned buf) {
    pcm_count_t *count = &pcm_count_adpcm[kind];

    if (!pcm_ready || !pcm_adpcm_ok)
        return;

    if (pcm_clock_pending)
        pcm_clock_settle();

    if (pcm_step_pending) {
        if (pcm_irq_count != pcm_step_irq_base)
            count->got++;
        else
            pcm_recover();
    }

    if (pcm_adpcm_fill.active && pcm_adpcm_fill.buf == buf)
        pcm_adpcm_fill_samples(0xFFFF);

    pcm_step_irq_base = pcm_irq_count;
    dma_program(sb_dma, pcm_step_buf[buf], pcm_step_len[buf], DMA_MODE_SINGLE);
    count->expected++;
    pcm_step_pending = pcm_dsp_play(pcm_adpcm_formats[format].command, pcm_step_len[buf]);
    pcm_dsp_busy = pcm_step_pending;
}

void pcm_adpcm_steps_end(pcm_steps_t kind) {
    pcm_count_t *count = &pcm_count_adpcm[kind];

    if (!pcm_ready || !pcm_adpcm_ok || !pcm_step_pending)
        return;

    if (pcm_irq_count != pcm_step_irq_base) {
        count->got++;
        dma_mask(sb_dma);
        pcm_dsp_busy = 0;
    } else
        pcm_recover();

    pcm_step_pending = 0;
}
