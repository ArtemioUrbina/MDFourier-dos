#include <stdio.h>
#include <math.h>
#include "mdf.h"
#include "vsync.h"

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

#define MDF_MAX_STEPS 256

/* Sweep register values, computed once before the sequence starts */
static uint8_t  sweep_block[MDF_MAX_STEPS];
static uint16_t sweep_fnum[MDF_MAX_STEPS];

static void silence(unsigned frames)
{
    unsigned i;

    for (i = 0; i < frames; i++)
        vsync_wait();
}

/* Load the SINE instrument and the reference frequency 
 * Call at least one frame before pulse_train()
 */
static void pulse_prepare(uint8_t channel)
{
    uint8_t  block;
    uint16_t fnum;

    opl_set_instrument(channel, OPL_INSTRUMENT_SINE);
    opl_freq_to_block_fnum(REFERENCE_HZ, &block, &fnum);
    opl_note_set(channel, block, fnum);
}

/* Must be entered right after a vsync_wait(), with pulse_prepare()
 * already done.
 */
static unsigned pulse_train(uint8_t channel)
{
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
static unsigned sweep_freq(unsigned step, unsigned steps)
{
    double t = (steps > 1) ? (double)step / (double)(steps - 1) : 0.0;
    double f = (double)SWEEP_MIN_HZ *
               pow((double)SWEEP_MAX_HZ / (double)SWEEP_MIN_HZ, t);
    return (unsigned)(f + 0.5);
}

static void sweep_build_table(unsigned steps)
{
    unsigned step;

    for (step = 0; step < steps; step++)
        opl_freq_to_block_fnum(sweep_freq(step, steps),
                               &sweep_block[step], &sweep_fnum[step]);
}

/* Must be entered right after a vsync_wait(), with the instrument
 * already loaded and sweep_build_table() already run. */
static unsigned long run_sweep(uint8_t channel, unsigned frames,
                               unsigned steps)
{
    unsigned step, frame, release_frame;
    unsigned long total_frames = 0;

    release_frame = frames - frames / 5;

    for (step = 0; step < steps; step++) {
        opl_note_on_raw(channel, sweep_block[step], sweep_fnum[step]);
        for (frame = 0; frame < frames; frame++) {
            if (frame == release_frame)
                opl_note_off(channel);
            vsync_wait();
        }
        total_frames += frames;
    }
    opl_note_off(channel);

    return total_frames;
}

void mdf_run_full_test(uint8_t channel, unsigned frames, unsigned steps)
{
    unsigned long total_frames = 0;

    if (steps > MDF_MAX_STEPS)
        steps = MDF_MAX_STEPS;
    if (steps == 0)
        steps = 1;

    /* Everything slow here */
    printf("Frequency sweep: %u steps, %u frames each, %uHz to %uHz\n",
           steps, frames, SWEEP_MIN_HZ, SWEEP_MAX_HZ);

    sweep_build_table(steps);

    pulse_prepare(channel);

    /* Align the first key-on to a frame edge */
    vsync_wait();

    /* MDF Sequence */
    total_frames += pulse_train(channel);

    silence(SILENCE_FRAMES);
    total_frames += SILENCE_FRAMES;

    /* Silence, loading the sweep instrument one frame before it ends */
    silence(SILENCE_FRAMES - 1);
    opl_set_instrument(channel, OPL_INSTRUMENT_DEFAULT);
    silence(1);
    total_frames += SILENCE_FRAMES;

    total_frames += run_sweep(channel, frames, steps);

    /* Decay */
    silence(DECAY_FRAMES);
    total_frames += DECAY_FRAMES;

    /* Silence, preparing the end marker one frame before it ends */
    silence(SILENCE_FRAMES - 1);
    pulse_prepare(channel);
    silence(1);
    total_frames += SILENCE_FRAMES;

    total_frames += pulse_train(channel);

    printf("\nSequence complete: %lu frames\n", total_frames);
}
