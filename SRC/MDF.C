#include <stdio.h>
#include "mdf.h"
#include "vsync.h"
#include "kbd.h"
#include "sweep.h"

/*
 * TIMING
 *
 * Every key-on / key-off in the measured sequence is issued right
 * after vsync_wait() returns, with nothing slow in between.
 *
 */

#define PULSE_COUNT  10
#define SILENCE_FRAMES 20
#define DECAY_FRAMES 5
#define CHIPID_FRAMES 60

/* ESC pressed */
static int aborted;          
static int stereo;

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

/* Load the SINE instrument and the reference frequency
 * Call at least one frame before pulse_train()
 */
static void pulse_prepare(uint8_t channel) {
    if (stereo)
        opl_set_pan(channel, OPL_PAN_CENTER);
    opl_set_instrument(channel, OPL_INSTRUMENT_SINE);
    opl_note_set(channel, REF_BLOCK, REF_FNUM);
}

/* OPL3-SAx, OPL3-L, OPL4 run from ~49516 Hz instead of 49716 Hz,
 * so it plays ~24 Hz lower at 6 kHz.
 *
 * Source: https://nerdlypleasures.blogspot.com/2018/01/opl23-frequency-1hz-ish-difference.html
 *
 * Values measured with MDFourier from real hardware, using -j for 1/16 Hz FFT
 * with peak interpolation (validated on a synthetic 6002.30 Hz tone):
 * ========================================================================
 * Card                         Fm Chip     Chip ID Tone    Estimated Clock
 * ========================================================================
 * Soundblaster 2.0 CT1336A     OPL2        6002.3139Hz     49717.8503Hz
 * SoundBlaster 16  CT1740      OPL3        6003.2374Hz     49725.4998Hz
 * Yamaha Audician 32 Plus      OPL3-SAx    5978.3547Hz     49519.3937Hz
 * CompaqÿES1869                ESS ESFM    5991.0690Hz     49624.7075Hz
 * ========================================================================
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

void sweep_prepare(uint8_t channel) {
    if (stereo) {
        opl_set_pan(channel, OPL_PAN_LEFT);
        opl_set_pan(channel + 1, OPL_PAN_RIGHT);
        opl_set_instrument(channel + 1, OPL_INSTRUMENT_DEFAULT);
    }
    opl_set_instrument(channel, OPL_INSTRUMENT_DEFAULT);
}

/* Left and right start one write apart */
void sweep_note_on(uint8_t channel, unsigned step) {
    if (stereo) {
        opl_note_set(channel, sweep[step].block, sweep[step].fnum);
        opl_note_set(channel + 1, sweep[step].block, sweep[step].fnum);
        opl_key_on(channel);
        opl_key_on(channel + 1);
    } else
        opl_note_on_raw(channel, sweep[step].block, sweep[step].fnum);
}

void sweep_note_off(uint8_t channel) {
    opl_note_off(channel);
    if (stereo)
        opl_note_off(channel + 1);
}

static unsigned long run_sweep(uint8_t channel, unsigned frames) {
    unsigned step, frame, release_frame;
    unsigned long total_frames = 0;

    release_frame = frames - frames / 5;

    for (step = 0; step < SWEEP_STEPS; step++) {
        sweep_note_on(channel, step);
        for (frame = 0; frame < frames; frame++) {
            if (frame == release_frame)
                sweep_note_off(channel);
            if (check_abort())
                return total_frames;
            vsync_wait();
        }
        total_frames += frames;
    }
    sweep_note_off(channel);

    return total_frames;
}

static int end_sequence(uint8_t channel, unsigned long total_frames) {
    vsync_stats_t stats;

    vsync_end(&stats);
    sweep_note_off(channel);

    if (aborted) {
        printf("\nAborted\n");
        total_frames = stats.frames;
    } else
        printf("\nSequence complete: %lu frames\n", total_frames);

    vsync_print_stats(&stats, total_frames);
    return aborted;
}

int mdf_run_full_test(uint8_t channel, unsigned frames, int use_stereo) {
    unsigned long total_frames = 0;

    stereo = use_stereo;

    /* Everything slow here */
    printf("Frequency sweep: %u steps, %u frames each, %uHz to %uHz\n",
           (unsigned)SWEEP_STEPS, frames, SWEEP_MIN_HZ, SWEEP_MAX_HZ);

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
    sweep_prepare(channel);
    silence(1);
    total_frames += SILENCE_FRAMES;
    if (aborted)
        return end_sequence(channel, total_frames);

    total_frames += run_sweep(channel, frames);
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
