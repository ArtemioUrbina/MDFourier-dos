#include <stdio.h>
#include "cmsseq.h"
#include "cms.h"
#include "seq.h"
#include "vsync.h"
#include "host.h"

/*
 * CMS / Game Blaster sequence, its own profile: DOS: Creative Music System
 *
 * Every write follows vsync_wait() right away, as in the FM sequence.
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
 * Frequency: 7159090 / 512 * 2^octave / (511 - freq)
 */

#define PULSE_COUNT     10
#define LEAD_FRAMES     4           /* sync tone loaded before the first pulse */
#define SILENCE_FRAMES  20
#define PAD_FRAMES      20
#define SETTLE_FRAMES   40          /* the chord's thump, before the silence */
#define CLK_FRAMES      60
#define WM_FRAMES       10
#define NOTE_FRAMES     20
#define LEVEL_FRAMES    20
#define VOICE_FRAMES    20
#define SEP_FRAMES      20
#define NOISE_FRAMES    40          /* longer: more bins per band */
#define ENV_FRAMES      40
#define STACK_FRAMES    20
#define CHORD_FRAMES    60

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

/* Voices: first chip's first one, second chip's first one */
#define LEFT_VOICE      0
#define RIGHT_VOICE     6

/* Every step stops at 80% with all levels at 0 */
void cmsseq_mute_all(void *context, int step) {
    (void)context;
    (void)step;
    cms_mute_all();
}

/* Silence with the next block's first step loaded at its start */
void cmsseq_gap(unsigned frames, seq_step_fn prep) {
    if (!seq_stopped() && prep)
        prep(NULL, 0);
    seq_silence(frames);
}

/* ---- Sync, clock and watermark: voice 1, both sides ---- */

void cmsseq_ref_prep(void *context, int step) {
    (void)context;
    (void)step;
    cms_tone(LEFT_VOICE, REF_OCT, REF_FREQ, 0, 0);
}

void cmsseq_ref_on(void *context, int step) {
    (void)context;
    (void)step;
    cms_level(LEFT_VOICE, MAX_LEVEL, MAX_LEVEL);
}

/* Tone loaded. Must be entered right after a vsync_wait() */
void cmsseq_pulse_train() {
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

int cmsseq_wm_ok;

void cmsseq_wm_prep(void *context, int step) {
    (void)context;
    (void)step;
    if (host_is_windows()) {
        if (cmsseq_wm_ok)
            cms_tone(LEFT_VOICE, WM_WIN_OK_OCT, WM_WIN_OK_FREQ, 0, 0);
        else
            cms_tone(LEFT_VOICE, WM_WIN_BAD_OCT, WM_WIN_BAD_FREQ, 0, 0);
    } else if (cmsseq_wm_ok)
        cms_tone(LEFT_VOICE, WM_OK_OCT, WM_OK_FREQ, 0, 0);
    else
        cms_tone(LEFT_VOICE, WM_BAD_OCT, WM_BAD_FREQ, 0, 0);
}

/* Held whole, muted at the end */
void cmsseq_hold_full(unsigned frames, seq_step_fn on) {
    if (seq_stopped())
        return;
    on(NULL, 0);
    seq_silence(frames);
    cms_mute_all();
}

/* ---- Notes: 96 semitones from A0, chips left and right ---- */

/* Semitones from A, the start of each octave (Creative's table) */
uint8_t cmsseq_semitone[12] = { 3, 31, 58, 83, 107, 130, 151, 172, 191, 209, 226, 242 };
#define NOTE_STEPS      96          /* A0 27.52 Hz to G#7 6.62 kHz */

void cmsseq_pair_prep(uint8_t octave, uint8_t freq) {
    cms_tone(LEFT_VOICE,  octave, freq, 0, 0);
    cms_tone(RIGHT_VOICE, octave, freq, 0, 0);
}

void cmsseq_pair_on(uint8_t level) {
    cms_level(LEFT_VOICE,  level, 0);
    cms_level(RIGHT_VOICE, 0, level);
}

void cmsseq_note_prep(void *context, int step) {
    (void)context;
    cmsseq_pair_prep((uint8_t)(step / 12), cmsseq_semitone[step % 12]);
}

void cmsseq_note_on(void *context, int step) {
    (void)context;
    (void)step;
    cmsseq_pair_on(MAX_LEVEL);
}

/* ---- Levels: 15 to 1 at A4 ---- */
#define LEVEL_STEPS     15

void cmsseq_level_prep(void *context, int step) {
    (void)context;
    (void)step;
    cmsseq_pair_prep(A4_OCT, A4_FREQ);
}

void cmsseq_level_on(void *context, int step) {
    (void)context;
    cmsseq_pair_on((uint8_t)(MAX_LEVEL - step));
}

/* ---- Voices: each of the 12 at A4, both sides ---- */

void cmsseq_voice_prep(void *context, int step) {
    (void)context;
    cms_tone(step, A4_OCT, A4_FREQ, 0, 0);
}

void cmsseq_voice_on(void *context, int step) {
    (void)context;
    cms_level(step, MAX_LEVEL, MAX_LEVEL);
}

/* ---- Separation: left, right, both at 1 kHz ---- */
#define SEP_STEPS       3
uint8_t cmsseq_sep_left[SEP_STEPS]  = { MAX_LEVEL, 0, MAX_LEVEL };
uint8_t cmsseq_sep_right[SEP_STEPS] = { 0, MAX_LEVEL, MAX_LEVEL };

void cmsseq_sep_prep(void *context, int step) {
    (void)context;
    (void)step;
    cms_tone(LEFT_VOICE, WM_OK_OCT, WM_OK_FREQ, 0, 0);
}

void cmsseq_sep_on(void *context, int step) {
    (void)context;
    cms_level(LEFT_VOICE, cmsseq_sep_left[step], cmsseq_sep_right[step]);
}

/* ---- Noise on voice 2 of each chip: the three fixed rates, then
 * clocked by voice 1 (silent) at four octaves of A. Random: compared by
 * averaged 1/3 octave bands ('b' in the profile) ---- */
#define NOISE_STEPS     7
uint8_t cmsseq_noise_octave[4] = { 2, 4, 6, 7 };

void cmsseq_noise_prep(void *context, int step) {
    (void)context;
    if (step < 3) {
        cms_noise(LEFT_VOICE + 1,  step, 0, 0);
        cms_noise(RIGHT_VOICE + 1, step, 0, 0);
    } else {
        cms_tone(LEFT_VOICE,  cmsseq_noise_octave[step - 3], A4_FREQ, 0, 0);
        cms_tone(RIGHT_VOICE, cmsseq_noise_octave[step - 3], A4_FREQ, 0, 0);
        cms_noise(LEFT_VOICE + 1,  CMS_NOISE_TONE, 0, 0);
        cms_noise(RIGHT_VOICE + 1, CMS_NOISE_TONE, 0, 0);
    }
}

void cmsseq_noise_on(void *context, int step) {
    (void)context;
    (void)step;
    cms_level(LEFT_VOICE + 1,  MAX_LEVEL, 0);
    cms_level(RIGHT_VOICE + 1, 0, MAX_LEVEL);
}

/* ---- Envelopes: generator 0 shapes voice 3, clocked by voice 2
 * (silent) at 55 Hz. Shapes 1-7: full, decay, repeated decay, triangle,
 * repeated triangle, attack, repeated attack. Writing the shape
 * restarts it ---- */
#define ENV_STEPS       7
#define ENV_CLK_OCT     1           /* A1, 55 Hz */

void cmsseq_env_prep(void *context, int step) {
    (void)context;
    (void)step;
    cms_envelope(0, 0, 0);
    cms_envelope(1, 0, 0);
    cms_tone(LEFT_VOICE + 1,  ENV_CLK_OCT, A4_FREQ, 0, 0);
    cms_tone(RIGHT_VOICE + 1, ENV_CLK_OCT, A4_FREQ, 0, 0);
    cms_tone(LEFT_VOICE + 2,  A4_OCT, A4_FREQ, 0, 0);
    cms_tone(RIGHT_VOICE + 2, A4_OCT, A4_FREQ, 0, 0);
}

void cmsseq_env_on(void *context, int step) {
    (void)context;
    cms_envelope(0, 0, (uint8_t)(CMS_ENV_ON | CMS_ENV_SHAPE(step + 1)));
    cms_envelope(1, 0, (uint8_t)(CMS_ENV_ON | CMS_ENV_SHAPE(step + 1)));
    cms_level(LEFT_VOICE + 2,  MAX_LEVEL, 0);
    cms_level(RIGHT_VOICE + 2, 0, MAX_LEVEL);
}

/* ---- Stack: A4 on 1 to 6 voices of each chip, restarted together so
 * the squares add in phase. Linear summing adds 20log(n): +6.0, +9.5,
 * +12.0, +14.0, +15.6 dB. Less shows compression or clipping ---- */
#define STACK_STEPS     6

void cmsseq_stack_prep(void *context, int step) {
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

void cmsseq_stack_on(void *context, int step) {
    int v;

    (void)context;
    for (v = 0; v <= step; v++) {
        cms_level(v,     MAX_LEVEL, 0);
        cms_level(v + 6, 0, MAX_LEVEL);
    }
    cms_sync();
}

/* ---- Chord: all six voices of each chip, C major over three octaves,
 * the sum ---- */
uint8_t cmsseq_chord_oct[6]  = { 2, 2, 2, 3, 3, 4 };
uint8_t cmsseq_chord_freq[6] = { 83, 172, 226, 83, 172, 83 };

void cmsseq_chord_prep(void *context, int step) {
    int v;

    (void)context;
    (void)step;
    cms_envelope(0, 0, 0);
    cms_envelope(1, 0, 0);
    for (v = 0; v < 6; v++) {
        cms_tone(v,     cmsseq_chord_oct[v], cmsseq_chord_freq[v], 0, 0);
        cms_tone(v + 6, cmsseq_chord_oct[v], cmsseq_chord_freq[v], 0, 0);
    }
}

void cmsseq_chord_level(uint8_t level) {
    int v;

    for (v = 0; v < 6; v++) {
        cms_level(v,     level, 0);
        cms_level(v + 6, 0, level);
    }
}

void cmsseq_chord_on(void *context, int step) {
    (void)context;
    (void)step;
    cmsseq_chord_level(MAX_LEVEL);
}

/* Down a level per frame instead of the big step of all twelve at once */
void cmsseq_chord_fade() {
    int level;

    for (level = MAX_LEVEL - 1; level >= 0 && !seq_stopped(); level--) {
        cmsseq_chord_level((uint8_t)level);
        seq_silence(1);
    }
}

int cmsseq_run(caps_t *caps) {
    int i;

    cmsseq_wm_ok = caps->mixer_ok;
    cms_reset();
    seq_begin(NULL);

    /* The sync tone running at level 0 before the first pulse */
    cmsseq_ref_prep(NULL, 0);
    seq_add_frames(LEAD_FRAMES);
    for (i = 0; i < LEAD_FRAMES; i++)
        vsync_wait();

    cmsseq_pulse_train();
    seq_silence(PAD_FRAMES);                    /* the pulses' thump */

    cmsseq_gap(SILENCE_FRAMES, cmsseq_ref_prep);
    cmsseq_hold_full(CLK_FRAMES, cmsseq_ref_on);              /* CMSCLK */

    cmsseq_gap(PAD_FRAMES, cmsseq_wm_prep);
    cmsseq_hold_full(WM_FRAMES, cmsseq_ref_on);               /* MIXER */

    cmsseq_gap(PAD_FRAMES, cmsseq_note_prep);
    seq_block(NOTE_STEPS, NOTE_FRAMES, cmsseq_note_prep, cmsseq_note_on, cmsseq_mute_all, NULL);
    cmsseq_gap(PAD_FRAMES, cmsseq_level_prep);
    seq_block(LEVEL_STEPS, LEVEL_FRAMES, cmsseq_level_prep, cmsseq_level_on, cmsseq_mute_all, NULL);
    cmsseq_gap(PAD_FRAMES, cmsseq_voice_prep);
    seq_block(CMS_VOICES, VOICE_FRAMES, cmsseq_voice_prep, cmsseq_voice_on, cmsseq_mute_all, NULL);
    cmsseq_gap(PAD_FRAMES, cmsseq_sep_prep);
    seq_block(SEP_STEPS, SEP_FRAMES, cmsseq_sep_prep, cmsseq_sep_on, cmsseq_mute_all, NULL);
    cmsseq_gap(PAD_FRAMES, cmsseq_noise_prep);
    seq_block(NOISE_STEPS, NOISE_FRAMES, cmsseq_noise_prep, cmsseq_noise_on, cmsseq_mute_all, NULL);
    cmsseq_gap(PAD_FRAMES, cmsseq_env_prep);
    seq_block(ENV_STEPS, ENV_FRAMES, cmsseq_env_prep, cmsseq_env_on, cmsseq_mute_all, NULL);
    cmsseq_gap(PAD_FRAMES, cmsseq_stack_prep);
    seq_block(STACK_STEPS, STACK_FRAMES, cmsseq_stack_prep, cmsseq_stack_on, cmsseq_mute_all, NULL);
    cmsseq_gap(PAD_FRAMES, cmsseq_chord_prep);
    if (!seq_stopped())
        cmsseq_chord_on(NULL, 0);
    seq_silence(CHORD_FRAMES - MAX_LEVEL);
    cmsseq_chord_fade();

    cmsseq_gap(SETTLE_FRAMES, NULL);
    cmsseq_gap(SILENCE_FRAMES, cmsseq_ref_prep);
    cmsseq_pulse_train();

    seq_stop();
    cms_silence();
    return seq_report();
}
