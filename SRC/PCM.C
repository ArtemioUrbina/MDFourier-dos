/*
 * PCM through the Sound Blaster DSP
 * 8-bit mono on every DSP, and in high-speed mode on DSP 2.01 and up
 * 8-bit stereo on DSP 3.xx (SB Pro method) and 4.xx (SB16 commands)
 * Stereo and 16-bit are in PCMX.C, Creative's ADPCM in ADPCM.C, the
 * 8259 and 8237 in IRQ.C and DMA.C. PCMINT.H is what they share
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
#include <string.h>
#include <math.h>
#include <conio.h>
#include <dos.h>
#include <i86.h>
#include "pcm.h"
#include "pcmdrv.h"
#include "pcmint.h"
#include "dsp.h"
#include "mixer.h"
#include "pit.h"
#include "host.h"

int8_t pcm_sine[SINE_SIZE];

/* The sine at each step amplitude, built once.
 * 8 Level steps and the sweep. */
int8_t pcm_amp_sine[AMP_TABLES][SINE_SIZE];

/* DMA buffers: clock, then two steps (one plays while the other is
 * built). From pcm_buf_alloc(), with the stereo ones */
uint8_t __far *pcm_clock_buf;
uint8_t __far *pcm_step_buf[2];
uint16_t       pcm_step_len[2];


int       pcm_ready;        /* pcm_init() passed, everything else checks it */
uint16_t  sb_base;
uint8_t   sb_irq, sb_dma;
uint8_t   pcm_dsp_major;        /* picks the commands: 1.xx to 4.xx differ */
uint8_t   pcm_dsp_minor;
int       pcm_hs_ok;            /* high-speed mode plays: DSP 2.01 and up, main sequence */
double    pcm_frame_rate;       /* measured, sizes the tones to the blocks */
irq_hook_t pcm_sb_hook;         /* put back at exit so DOS keeps working */

/* Transfers ended, from the ISRs */
volatile unsigned pcm_irq_count;

uint16_t  pcm_clock_len;        /* clock samples, a whole number of periods */
uint8_t   pcm_tc = PCM_TC;      /* the time constant playing, pcm_recover() sets it back */
int       pcm_dsp_busy;         /* a transfer may be running */

/* Further down */
int      pcm_buf_alloc(int stereo);
int      pcm_counts_ok();
unsigned pcm_transfers();
unsigned pcm_transfers_got();

/* Results */
pcm_count_t pcm_count_clock = { "clock", 0, 0 };
pcm_count_t pcm_count_steps[PCM_STEPS_COUNT] = {
    { "sweep", 0, 0 }, { "level", 0, 0 }, { "sweep", 0, 0 }, { "unfiltered", 0, 0 }, { "unfiltered", 0, 0 }
};
pcm_count_t pcm_count_hs_clock = { "clock", 0, 0 };
pcm_count_t pcm_count_adpcm_clock = { "clock", 0, 0 };
pcm_count_t pcm_count_adpcm[2] = { { "sweep", 0, 0 }, { "level", 0, 0 } };

/* The clock playing, 8-bit or ADPCM: its IRQ can be counted late */
pcm_count_t *pcm_clock_count = &pcm_count_clock;

/* pcm_irq_count when the step started: still the same at the next start
 * means its IRQ never came */
unsigned pcm_step_irq_base;
int      pcm_step_pending;

/* The 16-bit stereo driver, NULL when 16-bit doesn't play */
pcm16_driver_t *pcm16;


/* ---- 8259 ---- */
/* One IRQ per transfer, at its end. The DSP keeps its IRQ line raised
 * until acked by reading a port, without that no further IRQ comes.
 * DSP 4.xx has two sources on the one line, 8-bit and 16-bit transfers,
 * each acked on its own port, mixer 0x82 tells which. If it reads
 * neither (a clone without it), ack the 8-bit one */
void __interrupt __far pcm_isr() {
    /* A 16-bit driver sharing the line, the WSS codec */
    if (pcm16 && pcm16->shared_isr && pcm16->shared_isr()) {
        pic_eoi(sb_irq);
        return;
    }

    if (pcm_dsp_major >= 4) {
        uint8_t irq_status = mix_read(sb_base, SB16_IRQ_STATUS);

        if (irq_status & 2)
            inp(sb_base + DSP_ACK_16);

        if ((irq_status & 1) || !(irq_status & 3))
            inp(sb_base + DSP_READ_READY);
    } else
        inp(sb_base + DSP_READ_READY);

    pcm_irq_count++;
    pic_eoi(sb_irq);
}

void pcm_unhook() {
    irq_hook_remove(&pcm_sb_hook);
}

/* ---- 8237 ---- */
/* ---- DSP ---- */

void pcm_build_tables() {
    unsigned pos, table;

    for (pos = 0; pos < SINE_SIZE; pos++)
        pcm_sine[pos] = (int8_t)floor(127.0 * sin(2.0 * 3.14159265358979 * pos / SINE_SIZE) + 0.5);

    for (table = 0; table < AMP_TABLES; table++) {
        /* Of 256: halving the amplitude is 6 dB per Level step */
        int amp = table == AMP_SWEEP_TAB ? SWEEP_AMP : (256 >> table);

        for (pos = 0; pos < SINE_SIZE; pos++) {
            int scaled = pcm_sine[pos] * amp;      /* +-127 * 256 fits an int */

            /* Rounded away from zero on both sides, so the negative
             * half mirrors the positive one. No DC offset */
            pcm_amp_sine[table][pos] = (int8_t)((scaled + (scaled >= 0 ? 128 : -128)) / 256);
        }
    }

    /* sin at rate / 8 starting at 45 degrees: the period's last
     * sample is 0x80 and the next one carries on the same sine */
    for (pos = 0; pos < CLK_MAX; pos++)
        pcm_clock_buf[pos] = (uint8_t)(128 + floor(CLK_AMP *
                         sin(2.0 * 3.14159265358979 * ((pos % CLK_PERIOD) + 1) / CLK_PERIOD) + 0.5));
}

/* A DSP command with a length, which the DSP also takes as length - 1 */
int pcm_dsp_play(uint8_t cmd, uint16_t len) {
    uint16_t len_minus_1 = (uint16_t)(len - 1);

    return dsp_write(sb_base, cmd) &&
           dsp_write(sb_base, (uint8_t)(len_minus_1 & 0xFF)) &&
           dsp_write(sb_base, (uint8_t)(len_minus_1 >> 8));
}

/* A transfer left running. Stop it and get the DSP back. The reset
 * stops anything but forgets the rate and the speaker.
 * The rate goes before the speaker on: on DSP 1.xx/2.xx that one takes ~112 ms and
 * can drop what follows it */
void pcm_recover() {
    dma_mask(sb_dma);
    dsp_reset(sb_base);
    dsp_write(sb_base, DSP_TIME_CONST);
    dsp_write(sb_base, pcm_tc);
    dsp_write(sb_base, DSP_SPEAKER_ON);
    pcm_dsp_busy = 0;
}

/* The line the DSP raises at the end of a transfer. A wrong BLASTER I
 * plays every tone but no IRQ counts it: each step is taken as stuck
 * and reset, which on DSP 3.xx loses the stereo channel order. So a
 * 16-byte silent transfer with interrupts off, before the handler goes
 * in, and see which request goes up and drops once the DSP is acked.
 * 0 if none or more than one line moved */
#define IRQ_TEST_BYTES  16
#define IRQ_TEST_WAIT   PIT_US_TO_CLK(20000UL)

uint8_t pcm_dsp_probe_irq() {
    uint16_t before, during, after;
    uint32_t start;
    unsigned pos;

    for (pos = 0; pos < IRQ_TEST_BYTES; pos++)
        pcm_step_buf[0][pos] = 0x80;

    _disable();
    dsp_write(sb_base, DSP_TIME_CONST);
    dsp_write(sb_base, PCM_TC);
    dma_program(sb_dma, pcm_step_buf[0], IRQ_TEST_BYTES, DMA_MODE_SINGLE);
    before = pic_irr();
    pcm_dsp_play(DSP_PLAY_8BIT, IRQ_TEST_BYTES);

    /* 0.7 ms of samples, the line up soon after */
    start = pit_now();
    while (!((pic_irr() & ~before) & IRQ_CANDIDATES))
        if ((uint32_t)(pit_now() - start) > IRQ_TEST_WAIT)
            break;
    during = pic_irr();

    inp(sb_base + DSP_READ_READY);          /* ack, the line drops */
    after = pic_irr();
    dma_mask(sb_dma);
    _enable();

    return irq_line_of(before, during, after);
}

int pcm_irq_wrong_line;

int pcm_irq_mismatch() {
    return pcm_irq_wrong_line;
}

int pcm_init(blaster_cfg_t *cfg, double frame_hz, int extended) {
    uint8_t found_irq;

    dsp_version_t ver;

    pcm_ready = 0;
    if (cfg->port == -1 || cfg->irq == -1 || cfg->dma == -1) {
        printf(" PCM: BLASTER needs A, I and D\n");
        return 0;
    }
    if (cfg->dma > 3 || cfg->irq < 2 || cfg->irq > 15) {
        printf(" PCM: unsupported I%d or D%d\n", cfg->irq, cfg->dma);
        return 0;
    }
    sb_base = (uint16_t)cfg->port;
    /* On an AT, IRQ 2 is the cascade: cards on the IRQ 2 pin arrive as 9 */
    sb_irq  = (uint8_t)(cfg->irq == 2 ? 9 : cfg->irq);
    sb_dma  = (uint8_t)cfg->dma;
    pcm_frame_rate = frame_hz;

    if (!dsp_reset(sb_base) || !dsp_get_version(sb_base, &ver)) {
        printf(" PCM: DSP not answering\n");
        return 0;
    }

    pcm_dsp_major = ver.major;
    pcm_dsp_minor = ver.minor;
    pcm_hs_ok     = 0;

    if (!pcm_buf_alloc(extended && pcm_dsp_major >= 3)) {
        printf(" PCM: no memory for buffers\n");
        return 0;
    }

    found_irq = pcm_dsp_probe_irq();
    /* Windows virtualizes the PIC, its IRR may not show the real line */
    if (found_irq && found_irq != sb_irq && host_is_windows())
        printf(" PCM: test transfer raised IRQ %u, BLASTER says I%d. Windows virtualizes\n"
               "      the PIC, check BLASTER I if the IRQ counts come out missing\n",
               found_irq, cfg->irq);
    else if (found_irq && found_irq != sb_irq) {
        printf(" PCM: the DSP raised IRQ %u, BLASTER says I%d. Set BLASTER I%u\n"
               "      (or fix the card's setup) and run again\n", found_irq, cfg->irq, found_irq);
        pcm_irq_wrong_line = 1;
        return 0;
    }
    if (!found_irq && !host_is_windows())
        printf(" PCM: no IRQ seen in a test transfer, check BLASTER I%d and the card's setup\n",
               cfg->irq);

    /* Slow on a 486 SX since sin() is emulated, alert the user we are working */
    printf(" PCM: building tables...\n");
    pcm_build_tables();
    irq_hook_install(&pcm_sb_hook, sb_irq, pcm_isr);
    atexit(pcm_unhook);
    pcm_ready = 1;
    pcm_hs_ok = extended && (ver.major > 2 || (ver.major == 2 && ver.minor >= 1));

    if (verbose) {
        printf(" PCM: 8-bit mono, %.1f Hz, IRQ %u, DMA %u\n", PCM_RATE, sb_irq, sb_dma);
        if (pcm_hs_ok)
            printf(" PCM high-speed: 8-bit mono, %.1f Hz\n", PCM_HS_RATE);
    }
    if (extended)
        pcmx_init(cfg);
    return 1;
}

/* On DSP 1.xx/2.xx the speaker on takes ~112 ms and the DSP drops commands meanwhile */
void pcm_prepare() {
    if (!pcm_ready)
        return;

    dsp_write(sb_base, DSP_SPEAKER_ON);
}

/* ---- Clock tone ---- */

int pcm_hs_enabled() {
    return pcm_ready && pcm_hs_ok;
}

/* The clock armed: plays in high-speed mode, and was started */
int pcm_clock_hs;
int pcm_clock_armed;
int pcm_clock_playing;

/* In the gap's last frame: everything but the start, so the start on
 * the frame edge is a single DSP command, as quick as it gets */
void pcm_clock_setup(unsigned frames, double rate, uint8_t tc, int hs, pcm_count_t *count) {
    unsigned long total;

    /* Whole periods, so it ends on 0x80. Under 64 KB, one DSP count */
    total = (unsigned long)(rate * frames * CLK_PCT / 100.0 / pcm_frame_rate);
    total -= total % CLK_PERIOD;
    if (total > 0xFFF8UL)
        total = 0xFFF8UL;
    pcm_clock_len = (uint16_t)total;
    count->expected = 1;
    pcm_clock_count = count;
    pcm_clock_hs    = hs;
    pcm_clock_armed = 1;

    /* Rate set here, frames after the speaker on. The DMA loops the
     * buffer, the DSP count says when to stop */
    pcm_tc = tc;
    dsp_write(sb_base, DSP_TIME_CONST);
    dsp_write(sb_base, tc);
    dma_program(sb_dma, pcm_clock_buf, CLK_MAX, DMA_MODE_AUTO);
}

void pcm_clock_arm(unsigned frames) {
    if (pcm_ready)
        pcm_clock_setup(frames, PCM_RATE, PCM_TC, 0, &pcm_count_clock);
}

void pcm_hs_clock_arm(unsigned frames) {
    if (pcm_hs_enabled())
        pcm_clock_setup(frames, PCM_HS_RATE, PCM_HS_TC, 1, &pcm_count_hs_clock);
}

/* On the frame edge, one single-cycle transfer for the whole tone.
 * High-speed takes its length from the block size command */
void pcm_clock_start() {
    int started;

    if (!pcm_ready || !pcm_clock_armed)
        return;

    pcm_clock_armed   = 0;
    pcm_clock_playing = 1;
    pcm_irq_count = 0;
    if (pcm_clock_hs)
        started = pcm_dsp_play(DSP_BLOCK_SIZE, pcm_clock_len) && dsp_write(sb_base, DSP_HS_8BIT);
    else
        started = pcm_dsp_play(DSP_PLAY_8BIT, pcm_clock_len);
    if (started)
        pcm_dsp_busy = 1;
}

/* One IRQ should have come
 * The clock's IRQ can come after its block ends: the transfer fills 95% of it, ~43 ms to spare.
 *
 * A DOS box in Windows can be held longer or SB16 emulation (SB Live!, DOS and Windows)
 * signals its end late. It is counted at the next start instead, in the idle block */
int pcm_clock_pending;

void pcm_clock_settle() {
    pcm_clock_playing = 0;
    pcm_clock_count->got = pcm_irq_count >= 1;

    if (pcm_irq_count >= 1) {
        dma_mask(sb_dma);
        pcm_dsp_busy = 0;
    } else
        pcm_recover();
    pcm_clock_pending = 0;
}

void pcm_clock_stop() {
    if (!pcm_ready || !pcm_clock_playing)
        return;

    if (pcm_irq_count < 1)
        pcm_clock_pending = 1;
    else
        pcm_clock_settle();
}

/* ---- Steps ---- */

/* Log spaced. The same ratio between steps, like the FM sweep */
int pcm_kind_hs(pcm_steps_t kind) {
    return kind == PCM_HS_SWEEP || kind == PCM_HS_SWEEP_NF;
}

double pcm_step_freq(pcm_steps_t kind, unsigned step) {
    /* The same tones as the sweep's upper steps */
    if (kind == PCM_SWEEP_NF)
        return pcm_step_freq(PCM_SWEEP, step + PCM_SWEEP_STEPS - PCM_NF_STEPS);
    if (kind == PCM_HS_SWEEP_NF)
        return pcm_step_freq(PCM_HS_SWEEP, step + PCM_HS_SWEEP_STEPS - PCM_NF_STEPS);
    if (kind == PCM_LEVEL)
        return LEVEL_FREQ;
    if (kind == PCM_HS_SWEEP)
        return SWEEP_LOW * pow(HS_SWEEP_HIGH / SWEEP_LOW, (double)step / (PCM_HS_SWEEP_STEPS - 1));
    return SWEEP_LOW * pow(SWEEP_HIGH / SWEEP_LOW, (double)step / (PCM_SWEEP_STEPS - 1));
}

/* The kinds this card plays */
int pcm_steps_enabled(pcm_steps_t kind) {
    return pcm_kind_hs(kind) ? pcm_hs_enabled() : pcm_ready;
}

/* The step being built, a chunk per frame.
 * Phase is a 32-bit accumulator. A full turn of the sine is 2^32, each
 * sample adds phase_inc = freq / rate * 2^32.
 * Its top SINE_BITS bits index the table.
 * Any frequency, precise to ~5 uHz with no floating point per sample */
struct {
    int            active;
    unsigned       buf;
    uint8_t __far  *dest;
    int8_t         *table;
    unsigned       len, pos;
    uint32_t       phase, phase_inc;
} pcm_fill;

void pcm_fill_samples(unsigned max_samples) {
    unsigned end, pos;

    if (!pcm_fill.active)
        return;

    end = pcm_fill.pos + max_samples;
    if (end > pcm_fill.len)
        end = pcm_fill.len;

    for (pos = pcm_fill.pos; pos < end; pos++) {
        int sample = pcm_fill.table[(uint16_t)(pcm_fill.phase >> 16) >> (16 - SINE_BITS)];

        if (pos < FADE)
            sample = sample * (int)pos / FADE;
        else if (pos >= pcm_fill.len - FADE)
            sample = sample * (int)(pcm_fill.len - 1 - pos) / FADE;
        pcm_fill.dest[pos] = (uint8_t)(128 + sample);   /* 8-bit PCM is unsigned, 128 = 0 */
        pcm_fill.phase += pcm_fill.phase_inc;
    }
    pcm_fill.pos = end;
    if (pcm_fill.pos >= pcm_fill.len)
        pcm_fill.active = 0;
}

/* Sets up the build */
void pcm_step_fill(pcm_steps_t kind, unsigned step, unsigned buf, unsigned frames) {
    unsigned len;
    double   rate = pcm_kind_hs(kind) ? PCM_HS_RATE : PCM_RATE;

    if (!pcm_steps_enabled(kind))
        return;

    len = (unsigned)(rate * frames * FILL_PCT / 100.0 / pcm_frame_rate);
    if (len > STEP_MAX)
        len = STEP_MAX;
    pcm_step_len[buf] = (uint16_t)len;

    pcm_fill.buf       = buf;
    pcm_fill.dest      = pcm_step_buf[buf];
    pcm_fill.len       = len;
    pcm_fill.pos       = 0;
    pcm_fill.phase     = 0;
    pcm_fill.phase_inc = (uint32_t)(pcm_step_freq(kind, step) * 4294967296.0 / rate);
    pcm_fill.table     = pcm_amp_sine[kind == PCM_LEVEL ? step : AMP_SWEEP_TAB];
    pcm_fill.active    = 1;
}

void pcm_fill_more() {
    pcm_fill_samples(FILL_CHUNK);
    pcm_adpcm_fill_samples(ADPCM_CHUNK);
    pcmx_fill_samples(FILL_CHUNK);
    pcmx_poll();
}

/* Counts the previous step's IRQ, then starts this
 * one as a single-cycle transfer, one IRQ at its end */
void pcm_step_start(pcm_steps_t kind, unsigned buf) {
    pcm_count_t *count = &pcm_count_steps[kind];

    if (!pcm_steps_enabled(kind))
        return;

    if (pcm_clock_pending)
        pcm_clock_settle();

    if (pcm_step_pending) {
        if (pcm_irq_count != pcm_step_irq_base)
            count->got++;
        else
            pcm_recover();
    }

    /* Should be done by now, the rest if not. It costs frame time,
     * only on a CPU that can't keep up */
    if (pcm_fill.active && pcm_fill.buf == buf)
        pcm_fill_samples(STEP_MAX);

    pcm_step_irq_base = pcm_irq_count;
    dma_program(sb_dma, pcm_step_buf[buf], pcm_step_len[buf], DMA_MODE_SINGLE);
    count->expected++;
    if (pcm_kind_hs(kind))
        pcm_step_pending = pcm_dsp_play(DSP_BLOCK_SIZE, pcm_step_len[buf]) && dsp_write(sb_base, DSP_HS_8BIT);
    else
        pcm_step_pending = pcm_dsp_play(DSP_PLAY_8BIT, pcm_step_len[buf]);
    pcm_dsp_busy = pcm_step_pending;
}

/* After the high-speed steps a recovery sets the normal mode rate */
void pcm_steps_end(pcm_steps_t kind) {
    pcm_count_t *count = &pcm_count_steps[kind];

    if (pcm_kind_hs(kind))
        pcm_tc = PCM_TC;

    if (!pcm_steps_enabled(kind) || !pcm_step_pending)
        return;

    if (pcm_irq_count != pcm_step_irq_base) {
        count->got++;
        dma_mask(sb_dma);
        pcm_dsp_busy = 0;
    } else
        pcm_recover();

    pcm_step_pending = 0;
}

/* A DSP reset clicks only if something may still be playing */
void pcm_finish() {
    if (!pcm_ready)
        return;

    if (pcm_clock_pending)
        pcm_clock_settle();

    pcmx_finish();
    dma_mask(sb_dma);

    if (pcm_dsp_busy)
        dsp_reset(sb_base);

    dsp_write(sb_base, DSP_SPEAKER_OFF);
    pcm_dsp_busy = 0;
}

int pcm_print_count(pcm_count_t *count) {
    if (!count->expected)
        return 1;

    printf(" %s %u/%u", count->name, count->got, count->expected);
    return count->got == count->expected;
}

void pcm_print_report() {
    int ok;

    if (!pcm_ready)
        return;

    /* Every transfer ended with its IRQ: one line, the counts with /V */
    if (!verbose && pcm_counts_ok()) {
        printf("  PCM: %u transfers, all completed\n", pcm_transfers());
        return;
    }

    printf("  PCM IRQs:");
    ok  = pcm_print_count(&pcm_count_clock);
    ok &= pcm_print_count(&pcm_count_steps[PCM_SWEEP]);
    ok &= pcm_print_count(&pcm_count_steps[PCM_LEVEL]);
    ok &= pcm_print_count(&pcm_count_steps[PCM_SWEEP_NF]);
    printf("\n");
    if (pcm_count_hs_clock.expected) {
        printf("  PCM high-speed IRQs:");
        ok &= pcm_print_count(&pcm_count_hs_clock);
        ok &= pcm_print_count(&pcm_count_steps[PCM_HS_SWEEP]);
        ok &= pcm_print_count(&pcm_count_steps[PCM_HS_SWEEP_NF]);
        printf("\n");
    }
    if (pcm_count_adpcm_clock.expected) {
        printf("  ADPCM IRQs:");
        ok &= pcm_print_count(&pcm_count_adpcm_clock);
        ok &= pcm_print_count(&pcm_count_adpcm[PCM_SWEEP]);
        ok &= pcm_print_count(&pcm_count_adpcm[PCM_LEVEL]);
        printf("\n");
    }
    ok &= pcmx_print_report();

    if (!ok && !pcm_transfers_got()) {
        printf("  WARNING: PCM IRQ %u missing, check BLASTER I and the card's jumpers\n", sb_irq);
        /* Windows DMA buffer may be smaller than a tone loop buffer */
        if (host_is_windows())
            printf("  NOTE: under Windows, try [386Enh] DMABufferSize=64 in SYSTEM.INI\n");
    } else if (!ok)
        printf("  WARNING: %u of %u transfers without their IRQ in time, late or stopped\n",
               pcm_transfers() - pcm_transfers_got(), pcm_transfers());
}

/* ---- Memory: one DOS block, buffers placed so none crosses 64 KB ----
 * The 8237 can't cross a 64 KB physical page: its page register
 * doesn't carry. With stereo the buffers are ~75 KB, more than the
 * small model's single 64 KB data segment can spare: they come from
 * DOS, and each is placed so it stays within one page */

uint32_t pcm_buf_cursor, pcm_buf_end;   /* physical: next free byte, end of the block */

/* The next free spot that doesn't cross a 64 KB page, on an even
 * address since 16-bit DMA moves words */
void __far *pcm_buf_place(uint32_t bytes) {
    uint32_t phys = (pcm_buf_cursor + 1) & ~1UL;

    if ((phys >> 16) != ((phys + bytes - 1) >> 16))
        phys = (phys + 0xFFFFUL) & ~0xFFFFUL;      /* start of the next page */

    if (phys + bytes > pcm_buf_end)
        return 0;

    pcm_buf_cursor = phys + bytes;
    return MK_FP((unsigned)(phys >> 4), (unsigned)(phys & 15));
}

/* Mono buffers always, stereo ones from DSP 3.xx. A buffer that would
 * cross a page starts at the next one: each one under 21.5 KB, the
 * skips waste less than 64 KB in total */
#define MONO_NEED       (CLK_MAX + 2UL * STEP_MAX)
#define STEREO_NEED     (2UL * PCMX_STEP_FRAMES * 4 + PCMX_CLOCK_FRAMES * 4 + PCMX_CLOCK_FRAMES * 2)

int pcm_buf_alloc(int stereo) {
    unsigned seg;
    uint32_t total = MONO_NEED + (stereo ? STEREO_NEED : 0) + 0x10000UL;

    /* In 16-byte paragraphs */
    if (_dos_allocmem((unsigned)((total + 15) >> 4), &seg) != 0)
        return 0;

    pcm_buf_cursor = (uint32_t)seg << 4;
    pcm_buf_end    = pcm_buf_cursor + total;

    pcm_clock_buf   = (uint8_t __far *)pcm_buf_place(CLK_MAX);
    pcm_step_buf[0] = (uint8_t __far *)pcm_buf_place(STEP_MAX);
    pcm_step_buf[1] = (uint8_t __far *)pcm_buf_place(STEP_MAX);
    if (!pcm_clock_buf || !pcm_step_buf[0] || !pcm_step_buf[1])
        return 0;

    if (stereo) {
        pcmx_step_buf[0] = (uint8_t __far *)pcm_buf_place(PCMX_STEP_FRAMES * 4UL);
        pcmx_step_buf[1] = (uint8_t __far *)pcm_buf_place(PCMX_STEP_FRAMES * 4UL);
        pcmx_clock16_buf      = (int16_t __far *)pcm_buf_place(PCMX_CLOCK_FRAMES * 4UL);
        pcmx_clock8_buf       = (uint8_t __far *)pcm_buf_place(PCMX_CLOCK_FRAMES * 2UL);
    }
    return 1;
}

/* All counts as expected, and the 16-bit driver took its setup */
int pcm_counts_ok() {
    unsigned kind;

    if (pcm_count_clock.got != pcm_count_clock.expected ||
        pcm_count_hs_clock.got != pcm_count_hs_clock.expected || pcm16_failure)
        return 0;
    for (kind = 0; kind < PCM_STEPS_COUNT; kind++)
        if (pcm_count_steps[kind].got != pcm_count_steps[kind].expected)
            return 0;
    if (pcm_count_adpcm_clock.got != pcm_count_adpcm_clock.expected ||
        pcm_count_adpcm[PCM_SWEEP].got != pcm_count_adpcm[PCM_SWEEP].expected ||
        pcm_count_adpcm[PCM_LEVEL].got != pcm_count_adpcm[PCM_LEVEL].expected)
        return 0;
    for (kind = 0; kind < PCMX_KIND_COUNT; kind++)
        if (pcmx_counts[kind].got != pcmx_counts[kind].expected)
            return 0;
    return 1;
}

unsigned pcm_transfers_got() {
    unsigned kind, total;

    total = pcm_count_clock.got + pcm_count_hs_clock.got;
    for (kind = 0; kind < PCM_STEPS_COUNT; kind++)
        total += pcm_count_steps[kind].got;
    total += pcm_count_adpcm_clock.got + pcm_count_adpcm[PCM_SWEEP].got + pcm_count_adpcm[PCM_LEVEL].got;
    for (kind = 0; kind < PCMX_KIND_COUNT; kind++)
        total += pcmx_counts[kind].got;
    return total;
}

unsigned pcm_transfers() {
    unsigned kind, total;

    total = pcm_count_clock.expected + pcm_count_hs_clock.expected;
    for (kind = 0; kind < PCM_STEPS_COUNT; kind++)
        total += pcm_count_steps[kind].expected;
    total += pcm_count_adpcm_clock.expected + pcm_count_adpcm[PCM_SWEEP].expected + pcm_count_adpcm[PCM_LEVEL].expected;
    for (kind = 0; kind < PCMX_KIND_COUNT; kind++)
        total += pcmx_counts[kind].expected;
    return total;
}
