#include <stdio.h>
#include <math.h>
#include "mdf.h"
#include "vsync.h"
#include "kbd.h"

/*
 * TIMING
 *
 * Every key-on / key-off in the measured sequence is issued right
 * after vsync_wait() returns, with nothing slow in between.
 *
 */

#define SWEEP_MIN_HZ 20u
#define SWEEP_MAX_HZ 6200u   /* OPL_MAX_FREQ_HZ (6209) */
#define REFERENCE_HZ 6000u   /* pulse train / sync marker frequency */
#define PULSE_COUNT  10
#define SILENCE_FRAMES 20
#define DECAY_FRAMES 5
#define CHIPID_FRAMES 60

#define MDF_MAX_STEPS 256

/* Sweep register values, computed once before the sequence starts */
static uint8_t  sweep_block[MDF_MAX_STEPS];
static uint16_t sweep_fnum[MDF_MAX_STEPS];

/* ESC pressed */
static int aborted;          

static int check_abort() {
    if (!aborted && kbd_poll_escape())
        aborted = 1;
    return aborted;
}

static void silence(unsigned frames) {
    unsigned i;

    for (i = 0; i < frames; i++) {
        if (check_abort())
            return;
        vsync_wait();
    }
}

/* Reference tone registers, pre-computed  */
static uint8_t  ref_block;
static uint16_t ref_fnum;

/* Load the SINE instrument and the reference frequency
 * Call at least one frame before pulse_train()
 */
static void pulse_prepare(uint8_t channel) {
    opl_set_instrument(channel, OPL_INSTRUMENT_SINE);
    opl_note_set(channel, ref_block, ref_fnum);
}

/* OPL3-SAx, OPL3-L, OPL4) run from ~49516 Hz instead of 49716 Hz,
 * so it plays ~24 Hz lower at 6 kHz.
 *
 * Source: nerdlypleasures.blogspot.com/2018/01/opl23-frequency-1hz-ish-difference.html
 *
 * Values measured with MDFourier from real hardware for testing with profile:
 * CLK y 2 5996.0215 8.2915
 * SB 2.0       5996.0000Hz     49715.8340Hz
 * Auditian32   5972.0000Hz     49516.8380Hz
 * ES1869       5985.0000Hz     49624.6275Hz
 */
static unsigned chip_id(uint8_t channel) {
    unsigned i;

    opl_key_on(channel);
    for (i = 0; i < CHIPID_FRAMES; i++) {
        if (check_abort()) {
            opl_note_off(channel);
            return i;
        }
        vsync_wait();
    }
    opl_note_off(channel);

    return CHIPID_FRAMES;
}

/* Must be entered right after a vsync_wait(), with pulse_prepare()
 * already done.
 */
static unsigned pulse_train(uint8_t channel) {
    int i;

    for (i = 0; i < PULSE_COUNT; i++) {
        opl_key_on(channel);
        vsync_wait();
        opl_note_off(channel);
        vsync_wait();
    }

    return PULSE_COUNT * 2;
}

/* Log-spaced frequency for sweep step [0, steps). */
static unsigned sweep_freq(unsigned step, unsigned steps) {
    double t = (steps > 1) ? (double)step / (double)(steps - 1) : 0.0;
    double f = (double)SWEEP_MIN_HZ *
               pow((double)SWEEP_MAX_HZ / (double)SWEEP_MIN_HZ, t);
    return (unsigned)(f + 0.5);
}

static void sweep_build_table(unsigned steps) {
    unsigned step;

    for (step = 0; step < steps; step++)
        opl_freq_to_block_fnum(sweep_freq(step, steps),
                               &sweep_block[step], &sweep_fnum[step]);
}

/* Must be entered right after a vsync_wait(), with the instrument
 * already loaded and sweep_build_table() already run. */
static unsigned long run_sweep(uint8_t channel, unsigned frames, unsigned steps) {
    unsigned step, frame, release_frame;
    unsigned long total_frames = 0;

    release_frame = frames - frames / 5;

    for (step = 0; step < steps; step++) {
        opl_note_on_raw(channel, sweep_block[step], sweep_fnum[step]);
        for (frame = 0; frame < frames; frame++) {
            if (frame == release_frame)
                opl_note_off(channel);
            if (check_abort())
                return total_frames;
            vsync_wait();
        }
        total_frames += frames;
    }
    opl_note_off(channel);

    return total_frames;
}

static int end_sequence(uint8_t channel, unsigned long total_frames) {
    vsync_stats_t stats;

    vsync_end(&stats);
    opl_note_off(channel);

    if (aborted) {
        printf("\nAborted\n");
        total_frames = stats.frames;
    } else
        printf("\nSequence complete: %lu frames\n", total_frames);

    vsync_print_stats(&stats, total_frames);
    return aborted;
}

int mdf_run_full_test(uint8_t channel, unsigned frames, unsigned steps) {
    unsigned long total_frames = 0;

    if (steps > MDF_MAX_STEPS)
        steps = MDF_MAX_STEPS;
    if (steps == 0)
        steps = 1;

    /* Everything slow here */
    printf("Frequency sweep: %u steps, %u frames each, %uHz to %uHz\n",
           steps, frames, SWEEP_MIN_HZ, SWEEP_MAX_HZ);

    sweep_build_table(steps);
    opl_freq_to_block_fnum(REFERENCE_HZ, &ref_block, &ref_fnum);

    pulse_prepare(channel);

    aborted = 0;
    kbd_poll_escape();
    printf("Press ESC to abort\n");

    /* Align the first key-on to a frame edge */
    vsync_begin();

    /* MDF Sequence */
    total_frames += pulse_train(channel);

    silence(SILENCE_FRAMES);
    total_frames += SILENCE_FRAMES;
    if (aborted)
        return end_sequence(channel, total_frames);

    total_frames += chip_id(channel);
    if (aborted)
        return end_sequence(channel, total_frames);

    /* Silence, loading the sweep instrument one frame before it ends */
    silence(SILENCE_FRAMES - 1);
    opl_set_instrument(channel, OPL_INSTRUMENT_DEFAULT);
    silence(1);
    total_frames += SILENCE_FRAMES;
    if (aborted)
        return end_sequence(channel, total_frames);

    total_frames += run_sweep(channel, frames, steps);
    if (aborted)
        return end_sequence(channel, total_frames);

    /* Decay */
    silence(DECAY_FRAMES);
    total_frames += DECAY_FRAMES;

    /* Silence, preparing the end marker one frame before it ends */
    silence(SILENCE_FRAMES - 1);
    pulse_prepare(channel);
    silence(1);
    total_frames += SILENCE_FRAMES;
    if (aborted)
        return end_sequence(channel, total_frames);

    total_frames += pulse_train(channel);

    return end_sequence(channel, total_frames);
}
