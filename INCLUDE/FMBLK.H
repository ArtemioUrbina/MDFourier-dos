#ifndef FMBLK_H
#define FMBLK_H

#include "opltone.h"

/* Steps of the blocks played on the 440 Hz tone */
typedef enum {
    FMBLK_DEPTH,        /* FM_DEPTH instrument: modulator level */
    FMBLK_FEEDBACK,     /* FEEDBACK instrument: feedback 0-7 */
    FMBLK_LEVEL,        /* SINE instrument: carrier level */
    FMBLK_TREMOLO,      /* SINE instrument: tremolo */
    FMBLK_VIBRATO,      /* SINE instrument: vibrato */
    FMBLK_WAVE          /* SINE instrument: carrier waveform */
} fmblk_steps_t;

#define FMBLK_DEPTH_STEPS       8   /* modulator off (pure sine) to full */
#define FMBLK_FEEDBACK_STEPS    8
#define FMBLK_LEVEL_STEPS       8   /* 0 to -42 dB in 6 dB steps */
#define FMBLK_TREMOLO_STEPS     2   /* 1 and 4.8 dB */
#define FMBLK_VIBRATO_STEPS     2   /* 7 and 14 cents */
#define FMBLK_WAVE_STEPS        8   /* waveforms 0-7, OPL2 plays 4-7 as 0-3 */

void fmblk_init(int stereo);

void fmblk_ref_prepare();
void fmblk_watermark_prepare(int mixer_ok);
void fmblk_tone(unsigned frames);
void fmblk_pulse_train();
void fmblk_instrument(opl_instrument_t instrument);
void fmblk_sweep(unsigned frames, unsigned stride);
void fmblk_sine_sweep(unsigned frames);
void fmblk_steps(fmblk_steps_t kind, unsigned steps, unsigned frames);

void fmblk_filler_on();
void fmblk_filler_off();

void fmblk_off();

#endif
