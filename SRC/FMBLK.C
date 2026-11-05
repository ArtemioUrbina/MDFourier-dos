#include <stdio.h>
#include "fmblk.h"
#include "opl.h"
#include "seq.h"
#include "vsync.h"
#include "sweep.h"
#include "host.h"

/* The OPL channel the blocks play on, and the next one for the right side on an OPL3 */
#define FM_CHANNEL      0
#define PULSE_COUNT     10

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
 * Soundblaster 2.0 CT1350B     OPL2        6003.2305Hz     49725.4422Hz
 * SoundBlaster 16  CT1740      OPL3        6003.2335Hz     49725.4678Hz
 * SoundBlaster Vibra 16        CQM         6012.7485Hz     49804.2813Hz
 * Yamaha Audician 32 Plus      OPL3-SAx    5978.3358Hz     49519.2370Hz
 * Compaq ES1869                ESS ESFM    5991.0632Hz     49624.6592Hz
 * ESS Solo-1 ES1938            ESS ESFM    5990.1905Hz     49617.4307Hz
 * ========================================================================
 * The SB 2.0 and SB16 FM clocks seem to come from the ISA bus 14.31818 MHz
 * (/ 288 = 49715.9 Hz) so they might follow the PC, not the card.
 */

/* OPL3: left and right on two channels */
int fmblk_stereo;

void fmblk_init(int stereo) {
    fmblk_stereo = stereo;
}

/* Load the SINE instrument and the reference frequency */
void fmblk_ref_prepare() {
    if (fmblk_stereo)
        opl_set_pan(FM_CHANNEL, OPL_PAN_CENTER);
    opl_set_instrument(FM_CHANNEL, OPL_INSTRUMENT_SINE);
    opl_note_set(FM_CHANNEL, REF_BLOCK, REF_FNUM);
}

/* Under Windows 1500 and 2500 Hz */
void fmblk_watermark_prepare(int mixer_ok) {
    if (host_is_windows()) {
        if (mixer_ok)
            opl_note_set(FM_CHANNEL, WM_WIN_OK_BLOCK, WM_WIN_OK_FNUM);
        else
            opl_note_set(FM_CHANNEL, WM_WIN_BAD_BLOCK, WM_WIN_BAD_FNUM);
    } else if (mixer_ok)
        opl_note_set(FM_CHANNEL, WM_OK_BLOCK, WM_OK_FNUM);
    else
        opl_note_set(FM_CHANNEL, WM_BAD_BLOCK, WM_BAD_FNUM);
}

void fmblk_tone(unsigned frames) {
    if (seq_stopped())
        return;

    seq_add_frames(frames);
    opl_key_on(FM_CHANNEL);
    while (frames-- && seq_frame())
        ;
    opl_note_off(FM_CHANNEL);
}

void fmblk_pulse_train() {
    int i;

    if (seq_stopped())
        return;

    seq_add_frames(PULSE_COUNT * 2);
    for (i = 0; i < PULSE_COUNT; i++) {
        opl_key_on(FM_CHANNEL);
        vsync_wait();
        opl_note_off(FM_CHANNEL);
        vsync_wait();
    }
}

void fmblk_instrument(opl_instrument_t instrument) {
    if (fmblk_stereo) {
        opl_set_pan(FM_CHANNEL, OPL_PAN_LEFT);
        opl_set_pan(FM_CHANNEL + 1, OPL_PAN_RIGHT);
        opl_set_instrument(FM_CHANNEL + 1, instrument);
    }
    opl_set_instrument(FM_CHANNEL, instrument);
}

/* Left and right start one write apart */
void fmblk_note_on(sweep_note_t *n, int set_mult) {
    if (set_mult) {
        opl_set_carrier_mult(FM_CHANNEL, n->mult);
        if (fmblk_stereo)
            opl_set_carrier_mult(FM_CHANNEL + 1, n->mult);
    }
    if (fmblk_stereo) {
        opl_note_set(FM_CHANNEL, n->block, n->fnum);
        opl_note_set(FM_CHANNEL + 1, n->block, n->fnum);
        opl_key_on(FM_CHANNEL);
        opl_key_on(FM_CHANNEL + 1);
    } else
        opl_note_on_raw(FM_CHANNEL, n->block, n->fnum);
}

void fmblk_note_off() {
    opl_note_off(FM_CHANNEL);
    if (fmblk_stereo)
        opl_note_off(FM_CHANNEL + 1);
}

void fmblk_off() {
    fmblk_note_off();
}

typedef struct {
    fmblk_steps_t kind;
    sweep_note_t  *notes;
    unsigned      stride;       /* a sweep's step to note */
    int           set_mult;     /* the sine sweep's carrier multiple */
} fmblk_block_t;

/* Key-off at 80%, the rest is its release */
void fmblk_step_off(void *context, int step) {
    (void)context;
    (void)step;
    fmblk_note_off();
}

void fmblk_sweep_on(void *context, int step) {
    fmblk_block_t *block = (fmblk_block_t *)context;

    fmblk_note_on(&block->notes[step * block->stride], block->set_mult);
}

void fmblk_run_sweep(unsigned frames, sweep_note_t *notes, unsigned steps,
                     unsigned stride, int set_mult) {
    fmblk_block_t block;

    block.kind     = FMBLK_LEVEL;   /* not used by a sweep */
    block.notes    = notes;
    block.stride   = stride;
    block.set_mult = set_mult;
    seq_block((int)steps, frames, NULL, fmblk_sweep_on, fmblk_step_off, &block);
    fmblk_note_off();
}

void fmblk_sweep(unsigned frames, unsigned stride) {
    fmblk_run_sweep(frames, sweep, (SWEEP_STEPS + stride - 1) / stride, stride, 0);
}

void fmblk_sine_sweep(unsigned frames) {
    fmblk_run_sweep(frames, sine_sweep, SINE_STEPS, 1, 1);
}

/* Modulator levels for the FM depth block: off (pure sine) to full */
uint8_t fmblk_depth_level[FMBLK_DEPTH_STEPS] = { OPL_MOD_OFF, 48, 40, 32, 24, 16, 8, 0 };

/* Carrier levels for the level staircase: 0 to -42 dB in 6 dB steps */
uint8_t fmblk_car_level[FMBLK_LEVEL_STEPS] = { 0, 8, 16, 24, 32, 40, 48, 56 };

/* Tremolo 1 and 4.8 dB, vibrato 7 and 14 cents */
uint8_t fmblk_tremolo_depth[FMBLK_TREMOLO_STEPS] = { 0, OPL_DEPTH_AM_DEEP };
uint8_t fmblk_vibrato_depth[FMBLK_VIBRATO_STEPS] = { 0, OPL_DEPTH_VIB_DEEP };

void fmblk_step_set(uint8_t channel, fmblk_steps_t kind, unsigned step) {
    switch (kind) {
        case FMBLK_DEPTH:
            opl_set_mod_level(channel, fmblk_depth_level[step]);
            break;
        case FMBLK_FEEDBACK:
            opl_set_feedback(channel, (uint8_t)step);
            break;
        case FMBLK_LEVEL:
            opl_set_car_level(channel, fmblk_car_level[step]);
            break;
        case FMBLK_TREMOLO:
            opl_set_car_lfo(channel, OPL_LFO_AM);
            break;
        case FMBLK_VIBRATO:
            opl_set_car_lfo(channel, OPL_LFO_VIB);
            break;
        case FMBLK_WAVE:
            opl_set_car_wave(channel, (uint8_t)step);
            break;
    }
}

sweep_note_t fmblk_tone_440 = { TONE_BLOCK, TONE_FNUM, 1 };

void fmblk_steps_on(void *context, int step) {
    fmblk_block_t *block = (fmblk_block_t *)context;

    /* Global, shared by both channels */
    if (block->kind == FMBLK_TREMOLO)
        opl_set_lfo_depth(fmblk_tremolo_depth[step]);
    else if (block->kind == FMBLK_VIBRATO)
        opl_set_lfo_depth(fmblk_vibrato_depth[step]);
    fmblk_step_set(FM_CHANNEL, block->kind, (unsigned)step);
    if (fmblk_stereo)
        fmblk_step_set(FM_CHANNEL + 1, block->kind, (unsigned)step);
    fmblk_note_on(&fmblk_tone_440, 0);
}

void fmblk_steps(fmblk_steps_t kind, unsigned steps, unsigned frames) {
    fmblk_block_t block;

    block.kind     = kind;
    block.notes    = NULL;
    block.stride   = 1;
    block.set_mult = 0;
    seq_block((int)steps, frames, NULL, fmblk_steps_on, fmblk_step_off, &block);
    fmblk_note_off();
    if (kind == FMBLK_TREMOLO || kind == FMBLK_VIBRATO)
        opl_set_lfo_depth(0);
}

/* Filler, -15 dB at 440 Hz */
#define FILLER_HZ       440
#define FILLER_LEVEL    20      /* carrier attenuation, 0.75 dB steps: -15 dB */

void fmblk_filler_on() {
    if (fmblk_stereo)
        opl_set_pan(FM_CHANNEL, OPL_PAN_CENTER);

    opl_set_instrument(FM_CHANNEL, OPL_INSTRUMENT_SINE);
    opl_set_car_level(FM_CHANNEL, FILLER_LEVEL);
    opl_note_on(FM_CHANNEL, FILLER_HZ);
}

void fmblk_filler_off() {
    opl_note_off(FM_CHANNEL);
}
