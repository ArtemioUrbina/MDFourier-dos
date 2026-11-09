#ifndef CMSBLK_H
#define CMSBLK_H

/* CMS blocks for the sequences, cms_reset() done. A step's frequency is
 * loaded at level 0 ahead of it and started with the level alone, so
 * each block's first step goes in cmsblk_prepare() at the start of the
 * silence before it */

typedef enum {
    CMSBLK_NOTES,       /* 96 semitones from A0, chips left and right */
    CMSBLK_LEVELS,      /* A4 at levels 15 to 1 */
    CMSBLK_VOICES,      /* each of the 12 voices at A4, both sides */
    CMSBLK_SEPARATION,  /* left, right, both at 1 kHz */
    CMSBLK_NOISE,       /* the three noise rates, then four clocked by a voice */
    CMSBLK_ENVELOPE,    /* envelope shapes 1-7 */
    CMSBLK_STACK,       /* A4 on 1 to 6 voices of each chip, in phase */
    CMSBLK_CHORD        /* C major on all voices, faded out */
} cmsblk_kind_t;

#define CMSBLK_NOTES_STEPS      96
#define CMSBLK_LEVELS_STEPS     15
#define CMSBLK_VOICES_STEPS     12
#define CMSBLK_SEPARATION_STEPS 3
#define CMSBLK_NOISE_STEPS      7
#define CMSBLK_ENVELOPE_STEPS   7
#define CMSBLK_STACK_STEPS      6

/* Sync and clock tone, 6005.95 Hz */
void cmsblk_ref_prepare();
/* Under Windows 1500 and 2500 Hz */
void cmsblk_watermark_prepare(int mixer_ok);
/* The prepared tone held for frames, muted at the end */
void cmsblk_tone(unsigned frames);
/* Reference tone prepared, right after a frame edge */
void cmsblk_pulse_train();

void cmsblk_prepare(cmsblk_kind_t kind);
void cmsblk_steps(cmsblk_kind_t kind, unsigned steps, unsigned frames);
/* Held, then down a level per frame */
void cmsblk_chord(unsigned frames);

/* The filler for the blocks this card can't play: A4 at a low level */
void cmsblk_filler_on();
void cmsblk_filler_off();

void cmsblk_off();

#endif
