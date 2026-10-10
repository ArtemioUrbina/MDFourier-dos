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

#define PULSE_COUNT     10
#define SILENCE_FRAMES  20
#define DECAY_FRAMES    5
#define CHIPID_FRAMES   60
#define WM_FRAMES       10
#define SINE_FRAMES     10
#define STEP_FRAMES     20
#define LFO_FRAMES      70

/* ESC pressed */
int aborted;
int stereo;

/* Frames the blocks are meant to play, each adds its own length.
 * Checked against the frames vsync actually counted */
unsigned long seq_frames;

int check_abort() {
    if (!aborted && kbd_poll_escape())
        aborted = 1;
    return aborted;
}

int next_frame() {
    if (check_abort())
        return 0;
    vsync_wait();
    return 1;
}

void silence(unsigned frames) {
    seq_frames += frames;
    while (frames-- && next_frame())
        ;
}

/* Load the SINE instrument and the reference frequency
 * Call at least one frame before pulse_train()
 */
void pulse_prepare(uint8_t channel) {
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
 * Soundblaster 2.0 CT1350B     OPL2        6002.3139Hz     49717.8503Hz
 * SoundBlaster 16  CT1740      OPL3        6003.2374Hz     49725.4998Hz
 * Yamaha Audician 32 Plus      OPL3-SAx    5978.3547Hz     49519.3937Hz
 * Compaq ES1869                ESS ESFM    5991.0690Hz     49624.7075Hz
 * ========================================================================
 */

void tone_hold(uint8_t channel, unsigned frames) {
    if (aborted)
        return;

    seq_frames += frames;
    opl_key_on(channel);
    while (frames-- && next_frame())
        ;
    opl_note_off(channel);
}

/* Must be entered right after a vsync_wait(), with pulse_prepare()
 * already done.
 */
void pulse_train(uint8_t channel) {
    int i;

    if (aborted)
        return;

    seq_frames += PULSE_COUNT * 2;
    for (i = 0; i < PULSE_COUNT; i++) {
        opl_key_on(channel);
        vsync_wait();
        opl_note_off(channel);
        vsync_wait();
    }
}

void sweep_prepare(uint8_t channel, opl_instrument_t instrument) {
    if (stereo) {
        opl_set_pan(channel, OPL_PAN_LEFT);
        opl_set_pan(channel + 1, OPL_PAN_RIGHT);
        opl_set_instrument(channel + 1, instrument);
    }
    opl_set_instrument(channel, instrument);
}

/* Left and right start one write apart */
void sweep_note_on(uint8_t channel, const sweep_note_t *n, int set_mult) {
    if (set_mult) {
        opl_set_carrier_mult(channel, n->mult);
        if (stereo)
            opl_set_carrier_mult(channel + 1, n->mult);
    }
    if (stereo) {
        opl_note_set(channel, n->block, n->fnum);
        opl_note_set(channel + 1, n->block, n->fnum);
        opl_key_on(channel);
        opl_key_on(channel + 1);
    } else
        opl_note_on_raw(channel, n->block, n->fnum);
}

void sweep_note_off(uint8_t channel) {
    opl_note_off(channel);
    if (stereo)
        opl_note_off(channel + 1);
}

/* A note already keyed on. Key-off at 80%, the rest is its release */
void note_frames(uint8_t channel, unsigned frames) {
    unsigned frame, release_frame;

    seq_frames += frames;
    release_frame = frames - frames / 5;
    for (frame = 0; frame < frames; frame++) {
        if (frame == release_frame)
            sweep_note_off(channel);
        if (!next_frame())
            return;
    }
}

void run_sweep(uint8_t channel, unsigned frames, const sweep_note_t *notes, unsigned steps, int set_mult) {
    unsigned step;

    for (step = 0; step < steps && !aborted; step++) {
        sweep_note_on(channel, &notes[step], set_mult);
        note_frames(channel, frames);
    }
    sweep_note_off(channel);
}

#define COUNT(t) (sizeof(t) / sizeof(t[0]))

/* Modulator levels for the FM depth block: off (pure sine) to full */
uint8_t depth_level[] = { OPL_MOD_OFF, 48, 40, 32, 24, 16, 8, 0 };
#define DEPTH_STEPS COUNT(depth_level)
#define FEEDBACK_STEPS 8

/* Carrier levels for the level staircase: 0 to -42 dB in 6 dB steps */
uint8_t car_level[] = { 0, 8, 16, 24, 32, 40, 48, 56 };
#define LEVEL_STEPS COUNT(car_level)

/* Tremolo 1 and 4.8 dB, vibrato 7 and 14 cents */
typedef struct {
    uint8_t lfo;
    uint8_t depth;
} lft_st;

lft_st lfo_step[] = {
    { OPL_LFO_AM,  0 },
    { OPL_LFO_AM,  OPL_DEPTH_AM_DEEP },
    { OPL_LFO_VIB, 0 },
    { OPL_LFO_VIB, OPL_DEPTH_VIB_DEEP }
};

#define LFO_STEPS COUNT(lfo_step)

typedef enum {
    STEPS_DEPTH,        /* FM_DEPTH instrument: modulator level */
    STEPS_FEEDBACK,     /* FEEDBACK instrument: feedback 0-7 */
    STEPS_LEVEL,        /* SINE instrument: carrier level */
    STEPS_LFO           /* SINE instrument: tremolo, vibrato */
} steps_t;

void step_set(uint8_t channel, steps_t kind, unsigned step) {
    switch (kind) {
        case STEPS_DEPTH:
            opl_set_mod_level(channel, depth_level[step]);
            break;
        case STEPS_FEEDBACK:
            opl_set_feedback(channel, (uint8_t)step);
            break;
        case STEPS_LEVEL:
            opl_set_car_level(channel, car_level[step]);
            break;
        case STEPS_LFO:
            opl_set_car_lfo(channel, lfo_step[step].lfo);
            break;
    }
}

/* The 440 Hz tone, held once per step, a parameter changed before
 * each key-on: modulator level (FM depth) or feedback.
 * Must be entered right after a vsync_wait(), instrument loaded */
void run_steps(uint8_t channel, steps_t kind, unsigned steps, unsigned frames) {
    const sweep_note_t tone = { TONE_BLOCK, TONE_FNUM, 1 };
    unsigned step;

    for (step = 0; step < steps && !aborted; step++) {
        /* Global, shared by both channels */
        if (kind == STEPS_LFO)
            opl_set_lfo_depth(lfo_step[step].depth);
        step_set(channel, kind, step);
        if (stereo)
            step_set(channel + 1, kind, step);
        sweep_note_on(channel, &tone, 0);
        note_frames(channel, frames);
    }
    sweep_note_off(channel);
    if (kind == STEPS_LFO)
        opl_set_lfo_depth(0);
}

/* What a silence loads in its last frame, so the next block's first
 * key-on lands right on a frame edge */
typedef enum {
    PREP_NONE,
    PREP_PULSES,        /* SINE, reference note, centered */
    PREP_WM_OK,         /* watermark note: mixer verified */
    PREP_WM_BAD,        /* watermark note: mixer not verified */
    PREP_FM,            /* sweep instruments, left/right in stereo */
    PREP_SINE,          /* sweep sine */
    PREP_DEPTH,         /* LFO Depth  */
    PREP_FEEDBACK       /* Feedback Steps */
} prep_t;

void gap(uint8_t channel, unsigned frames, prep_t prep) {
    silence(frames - 1);
    if (aborted)
        prep = PREP_NONE;
    switch (prep) {
        case PREP_PULSES:
            pulse_prepare(channel);
            break;
        case PREP_WM_OK:
            opl_note_set(channel, WM_OK_BLOCK, WM_OK_FNUM);
            break;
        case PREP_WM_BAD:
            opl_note_set(channel, WM_BAD_BLOCK, WM_BAD_FNUM);
            break;
        case PREP_FM:
            sweep_prepare(channel, OPL_INSTRUMENT_DEFAULT);
            break;
        case PREP_SINE:
            sweep_prepare(channel, OPL_INSTRUMENT_SINE);
            break;
        case PREP_DEPTH:
            sweep_prepare(channel, OPL_INSTRUMENT_FM_DEPTH);
            break;
        case PREP_FEEDBACK:
            sweep_prepare(channel, OPL_INSTRUMENT_FEEDBACK);
            break;
        case PREP_NONE:
            break;
    }
    silence(1);
}

int end_sequence(uint8_t channel) {
    vsync_stats_t stats;
    unsigned long total_frames = seq_frames;

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

int mdf_run_full_test(uint8_t channel, unsigned frames, int use_stereo, int mixer_ok) {
    stereo     = use_stereo;
    aborted    = 0;
    seq_frames = 0;

    pulse_prepare(channel);

    kbd_poll_escape();
    printf("Press ESC to abort\n");

    /* Align the first key-on to a frame edge */
    vsync_begin();

    /* MDF Sequence */

    /* Sync */
    pulse_train(channel);                                       

    /* CHIPID */
    gap(channel, SILENCE_FRAMES, PREP_NONE);
    tone_hold(channel, CHIPID_FRAMES);                          

    /* MIXER */
    gap(channel, SILENCE_FRAMES, mixer_ok ? PREP_WM_OK : PREP_WM_BAD);
    tone_hold(channel, WM_FRAMES);                              

    /* FM */
    gap(channel, SILENCE_FRAMES, PREP_FM);
    run_sweep(channel, frames, sweep, SWEEP_STEPS, 0);          
    silence(DECAY_FRAMES);

    /* SINE */
    gap(channel, SILENCE_FRAMES, PREP_SINE);
    run_sweep(channel, SINE_FRAMES, sine_sweep, SINE_STEPS, 1);
    silence(DECAY_FRAMES);

    /* DEPTH */
    gap(channel, SILENCE_FRAMES, PREP_DEPTH);
    run_steps(channel, STEPS_DEPTH, DEPTH_STEPS, STEP_FRAMES);         

    /* FEEDBACK, FEEDBACK7 */
    gap(channel, SILENCE_FRAMES, PREP_FEEDBACK);
    run_steps(channel, STEPS_FEEDBACK, FEEDBACK_STEPS, STEP_FRAMES);   

    /* LEVEL */
    gap(channel, SILENCE_FRAMES, PREP_SINE);
    run_steps(channel, STEPS_LEVEL, LEVEL_STEPS, STEP_FRAMES);         

    /* LFO */
    gap(channel, SILENCE_FRAMES, PREP_SINE);
    run_steps(channel, STEPS_LFO, LFO_STEPS, LFO_FRAMES);              

    /* Sync */
    gap(channel, SILENCE_FRAMES, PREP_PULSES);
    pulse_train(channel);                                       

    return end_sequence(channel);
}
