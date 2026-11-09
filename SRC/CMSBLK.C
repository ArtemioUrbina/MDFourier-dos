#include <stdio.h>
#include "cmsblk.h"
#include "cms.h"
#include "seq.h"
#include "vsync.h"
#include "host.h"

/*
 * CMS / Game Blaster blocks
 *
 * Every write follows vsync_wait() right away, as in the FM blocks.
 * Stereo blocks play the first chip on the left and the second on the
 * right, so the two chips are compared against each other too.
 *
 * The SAA1099 takes a new frequency or octave at the end of the current
 * period, up to 36 ms at octave 0. So each step is loaded at level 0
 * during the silence before it, and notes start and stop with the level
 * alone, at once. Its output swings between 0 and the level: every
 * start and stop is also a DC step, the card's coupling turns it into
 * a slow thump. The silences are padded so it settles first.
 *
 * The generators run on their own once loaded, so a level write alone
 * starts the note at whatever point of its period it happens to be,
 * different in every run. Each step restarts both chips' generators
 * first, so it starts in the same phase on every run and recordings
 * can be compared in phase too.
 *
 * Frequency: 7159090 / 512 * 2^octave / (511 - freq)
 */

#define PULSE_COUNT     10

/* Sync and clock tone: octave 7, 213: 6005.948 Hz, the clock / 1192 */
#define REF_OCT         7
#define REF_FREQ        213

/* Watermark, as the FM ones: 1000.99 / 2001.98 Hz, under Windows
 * 1501.49 / 2499.68 Hz */
#define WM_OK_OCT       5
#define WM_OK_FREQ      64
#define WM_BAD_OCT      6
#define WM_BAD_FREQ     64
#define WM_WIN_OK_OCT   5
#define WM_WIN_OK_FREQ  213
#define WM_WIN_BAD_OCT  6
#define WM_WIN_BAD_FREQ 153

/* A4, 440.40 Hz */
#define A4_OCT          4
#define A4_FREQ         3

#define MAX_LEVEL       15

/* Filler: A4 at level 3 of 15, -14 dB */
#define FILLER_LEVEL    3

/* Voices: first chip's first one, second chip's first one */
#define LEFT_VOICE      0
#define RIGHT_VOICE     6

/* Every step stops at 80% with all levels at 0 */
void cmsblk_mute_all(void *context, int step) {
    (void)context;
    (void)step;
    cms_mute_all();
}

/* ---- Sync, clock and watermark: voice 1, both sides ---- */

void cmsblk_ref_prepare() {
    cms_tone(LEFT_VOICE, REF_OCT, REF_FREQ, 0, 0);
}

void cmsblk_watermark_prepare(int mixer_ok) {
    if (host_is_windows()) {
        if (mixer_ok)
            cms_tone(LEFT_VOICE, WM_WIN_OK_OCT, WM_WIN_OK_FREQ, 0, 0);
        else
            cms_tone(LEFT_VOICE, WM_WIN_BAD_OCT, WM_WIN_BAD_FREQ, 0, 0);
    } else if (mixer_ok)
        cms_tone(LEFT_VOICE, WM_OK_OCT, WM_OK_FREQ, 0, 0);
    else
        cms_tone(LEFT_VOICE, WM_BAD_OCT, WM_BAD_FREQ, 0, 0);
}

void cmsblk_tone(unsigned frames) {
    if (seq_stopped())
        return;
    cms_level(LEFT_VOICE, MAX_LEVEL, MAX_LEVEL);
    seq_silence(frames);
    cms_mute_all();
}

void cmsblk_pulse_train() {
    int i;

    if (seq_stopped())
        return;
    seq_add_frames(PULSE_COUNT * 2);
    for (i = 0; i < PULSE_COUNT; i++) {
        cms_level(LEFT_VOICE, MAX_LEVEL, MAX_LEVEL);
        vsync_wait();
        cms_level(LEFT_VOICE, 0, 0);
        vsync_wait();
    }
}

/* ---- Notes: 96 semitones from A0, chips left and right ---- */

/* Semitones from A, the start of each octave (Creative's table) */
uint8_t cmsblk_semitone[12] = { 3, 31, 58, 83, 107, 130, 151, 172, 191, 209, 226, 242 };

void cmsblk_pair_prep(uint8_t octave, uint8_t freq) {
    cms_tone(LEFT_VOICE,  octave, freq, 0, 0);
    cms_tone(RIGHT_VOICE, octave, freq, 0, 0);
}

void cmsblk_pair_on(uint8_t level) {
    cms_level(LEFT_VOICE,  level, 0);
    cms_level(RIGHT_VOICE, 0, level);
}

void cmsblk_note_prep(void *context, int step) {
    (void)context;
    cmsblk_pair_prep((uint8_t)(step / 12), cmsblk_semitone[step % 12]);
}

void cmsblk_note_on(void *context, int step) {
    (void)context;
    (void)step;
    cmsblk_pair_on(MAX_LEVEL);
}

/* ---- Levels: 15 to 1 at A4 ---- */

void cmsblk_level_prep(void *context, int step) {
    (void)context;
    (void)step;
    cmsblk_pair_prep(A4_OCT, A4_FREQ);
}

void cmsblk_level_on(void *context, int step) {
    (void)context;
    cmsblk_pair_on((uint8_t)(MAX_LEVEL - step));
}

/* ---- Voices: each of the 12 at A4, both sides ---- */

void cmsblk_voice_prep(void *context, int step) {
    (void)context;
    cms_tone(step, A4_OCT, A4_FREQ, 0, 0);
}

void cmsblk_voice_on(void *context, int step) {
    (void)context;
    cms_level(step, MAX_LEVEL, MAX_LEVEL);
}

/* ---- Separation: left, right, both at 1 kHz ---- */

uint8_t cmsblk_sep_left[CMSBLK_SEPARATION_STEPS]  = { MAX_LEVEL, 0, MAX_LEVEL };
uint8_t cmsblk_sep_right[CMSBLK_SEPARATION_STEPS] = { 0, MAX_LEVEL, MAX_LEVEL };

void cmsblk_sep_prep(void *context, int step) {
    (void)context;
    (void)step;
    cms_tone(LEFT_VOICE, WM_OK_OCT, WM_OK_FREQ, 0, 0);
}

void cmsblk_sep_on(void *context, int step) {
    (void)context;
    cms_level(LEFT_VOICE, cmsblk_sep_left[step], cmsblk_sep_right[step]);
}

/* ---- Noise on voice 2 of each chip: the three fixed rates, then
 * clocked by voice 1 (silent) at four octaves of A. Random: compared by
 * averaged 1/3 octave bands ('b' in the profile) ---- */

uint8_t cmsblk_noise_octave[4] = { 2, 4, 6, 7 };

void cmsblk_noise_prep(void *context, int step) {
    (void)context;
    if (step < 3) {
        cms_noise(LEFT_VOICE + 1,  step, 0, 0);
        cms_noise(RIGHT_VOICE + 1, step, 0, 0);
    } else {
        cms_tone(LEFT_VOICE,  cmsblk_noise_octave[step - 3], A4_FREQ, 0, 0);
        cms_tone(RIGHT_VOICE, cmsblk_noise_octave[step - 3], A4_FREQ, 0, 0);
        cms_noise(LEFT_VOICE + 1,  CMS_NOISE_TONE, 0, 0);
        cms_noise(RIGHT_VOICE + 1, CMS_NOISE_TONE, 0, 0);
    }
}

void cmsblk_noise_on(void *context, int step) {
    (void)context;
    (void)step;
    cms_level(LEFT_VOICE + 1,  MAX_LEVEL, 0);
    cms_level(RIGHT_VOICE + 1, 0, MAX_LEVEL);
}

/* ---- Envelopes: generator 0 shapes voice 3, clocked by voice 2
 * (silent) at 55 Hz. Shapes 1-7: full, decay, repeated decay, triangle,
 * repeated triangle, attack, repeated attack. Writing the shape
 * restarts it ---- */

#define ENV_CLK_OCT     1           /* A1, 55 Hz */

void cmsblk_env_prep(void *context, int step) {
    (void)context;
    (void)step;
    cms_envelope(0, 0, 0);
    cms_envelope(1, 0, 0);
    cms_tone(LEFT_VOICE + 1,  ENV_CLK_OCT, A4_FREQ, 0, 0);
    cms_tone(RIGHT_VOICE + 1, ENV_CLK_OCT, A4_FREQ, 0, 0);
    cms_tone(LEFT_VOICE + 2,  A4_OCT, A4_FREQ, 0, 0);
    cms_tone(RIGHT_VOICE + 2, A4_OCT, A4_FREQ, 0, 0);
}

void cmsblk_env_on(void *context, int step) {
    (void)context;
    cms_envelope(0, 0, (uint8_t)(CMS_ENV_ON | CMS_ENV_SHAPE(step + 1)));
    cms_envelope(1, 0, (uint8_t)(CMS_ENV_ON | CMS_ENV_SHAPE(step + 1)));
    cms_level(LEFT_VOICE + 2,  MAX_LEVEL, 0);
    cms_level(RIGHT_VOICE + 2, 0, MAX_LEVEL);
}

/* ---- Stack: A4 on 1 to 6 voices of each chip, restarted together
 * with the step so the squares add in phase. Linear summing adds 20log(n): +6.0, +9.5,
 * +12.0, +14.0, +15.6 dB. Less shows compression or clipping ---- */

void cmsblk_stack_prep(void *context, int step) {
    int v;

    (void)context;
    (void)step;
    cms_envelope(0, 0, 0);
    cms_envelope(1, 0, 0);
    for (v = 0; v < 6; v++) {
        cms_tone(v,     A4_OCT, A4_FREQ, 0, 0);
        cms_tone(v + 6, A4_OCT, A4_FREQ, 0, 0);
    }
}

void cmsblk_stack_on(void *context, int step) {
    int v;

    (void)context;
    for (v = 0; v <= step; v++) {
        cms_level(v,     MAX_LEVEL, 0);
        cms_level(v + 6, 0, MAX_LEVEL);
    }
}

/* ---- Chord: all six voices of each chip, C major over three octaves,
 * the sum ---- */

uint8_t cmsblk_chord_oct[6]  = { 2, 2, 2, 3, 3, 4 };
uint8_t cmsblk_chord_freq[6] = { 83, 172, 226, 83, 172, 83 };

void cmsblk_chord_prep(void *context, int step) {
    int v;

    (void)context;
    (void)step;
    cms_envelope(0, 0, 0);
    cms_envelope(1, 0, 0);
    for (v = 0; v < 6; v++) {
        cms_tone(v,     cmsblk_chord_oct[v], cmsblk_chord_freq[v], 0, 0);
        cms_tone(v + 6, cmsblk_chord_oct[v], cmsblk_chord_freq[v], 0, 0);
    }
}

void cmsblk_chord_level(uint8_t level) {
    int v;

    for (v = 0; v < 6; v++) {
        cms_level(v,     level, 0);
        cms_level(v + 6, 0, level);
    }
}

/* Down a level per frame instead of the big step of all twelve at once */
void cmsblk_chord(unsigned frames) {
    int level;

    if (seq_stopped())
        return;
    cms_sync();
    cmsblk_chord_level(MAX_LEVEL);
    seq_silence(frames - MAX_LEVEL);
    for (level = MAX_LEVEL - 1; level >= 0 && !seq_stopped(); level--) {
        cmsblk_chord_level((uint8_t)level);
        seq_silence(1);
    }
}

/* ---- Each kind's step functions ---- */

typedef struct {
    seq_step_fn prep;
    seq_step_fn on;
} cmsblk_block_t;

cmsblk_block_t cmsblk_blocks[] = {
    { cmsblk_note_prep,  cmsblk_note_on  },     /* CMSBLK_NOTES */
    { cmsblk_level_prep, cmsblk_level_on },     /* CMSBLK_LEVELS */
    { cmsblk_voice_prep, cmsblk_voice_on },     /* CMSBLK_VOICES */
    { cmsblk_sep_prep,   cmsblk_sep_on   },     /* CMSBLK_SEPARATION */
    { cmsblk_noise_prep, cmsblk_noise_on },     /* CMSBLK_NOISE */
    { cmsblk_env_prep,   cmsblk_env_on   },     /* CMSBLK_ENVELOPE */
    { cmsblk_stack_prep, cmsblk_stack_on },     /* CMSBLK_STACK */
    { cmsblk_chord_prep, NULL            }      /* CMSBLK_CHORD, cmsblk_chord() */
};

void cmsblk_prepare(cmsblk_kind_t kind) {
    if (!seq_stopped())
        cmsblk_blocks[kind].prep(NULL, 0);
}

/* Generators restarted, then the step's levels */
void cmsblk_step_on(void *context, int step) {
    cmsblk_block_t *block = (cmsblk_block_t *)context;

    cms_sync();
    block->on(NULL, step);
}

void cmsblk_steps(cmsblk_kind_t kind, unsigned steps, unsigned frames) {
    seq_block((int)steps, frames, cmsblk_blocks[kind].prep, cmsblk_step_on,
              cmsblk_mute_all, &cmsblk_blocks[kind]);
}

void cmsblk_filler_on() {
    cms_tone(LEFT_VOICE, A4_OCT, A4_FREQ, FILLER_LEVEL, FILLER_LEVEL);
}

void cmsblk_filler_off() {
    cms_mute_all();
}

void cmsblk_off() {
    cms_silence();
}
