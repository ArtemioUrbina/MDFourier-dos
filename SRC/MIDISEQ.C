#include <stdio.h>
#include "midiseq.h"
#include "midi.h"
#include "seq.h"
#include "vsync.h"
#include "pit.h"

/*
 * MIDI sequence (/M), a prototype: does MDFourier find a sync made of
 * MIDI notes, and does a held note measure the synth's tuning cleanly?
 *
 * It plays on MIDI channel 2: the MT-32 plays its first part there, GM
 * synths play every channel. A square wave gives the sync and the
 * tuning, a clean tone at the top of the range; the blocks play an
 * acoustic piano, program 0 on GM, GS, XG and the MT-32 alike. The
 * square is a different program number on the MT-32 (/MT).
 *
 *   Sync      10 pulses of C8 (4186 Hz), two frames on, two off
 *   Silence
 *   MIDICLK   A5 (880 Hz), the tuning: MDFourier reads it as A4 = 440 Hz
 *   Sweep     30 notes, A0 to C8 in minor thirds
 *   Level     A4, velocity 127 down to 15
 *   Pan       A4 left, right, center
 *   Silence
 *   Sync
 *
 * MDFourier finds the pulses in windows of a fraction of a millisecond,
 * where a tone near 6 kHz stands out best. C8 is the highest note every
 * synth plays (FluidSynth plays nothing above it with the soundfonts we
 * tried); the profile asks for 6000 Hz, which in those windows finds C8
 * too. A synth's attack and release blur a pulse of one frame, so ours
 * are two, and the profile says so (28 ms, Sync blocks of 40 frames).
 * Rendered offline at these timings with two GM soundfonts, both synced.
 *
 * A note off lets the note ring on through its release, longer than
 * the frame of silence between pulses. Each pulse is cut with the
 * expression controller instead, and the note released under it.
 * The expression comes back at the end of the gap after the pulses.
 */

#define MIDISEQ_CHANNEL     1       /* MIDI channel 2 */
#define PIANO_PROGRAM       0       /* acoustic piano, everywhere */
#define SQUARE_GM           80      /* GM Lead 1 (square) */
#define SQUARE_MT32         47      /* MT-32 Square Wave */

#define PULSE_COUNT     10
#define PULSE_FRAMES    2           /* on, then as many off */
#define SYNC_NOTE       108         /* C8, 4186.0 Hz */
#define CLOCK_NOTE      81          /* A5, 880 Hz */
#define TONE_NOTE       69          /* A4, 440 Hz */
#define TONE_VELOCITY   100

#define SWEEP_LOW       21          /* A0 */
#define SWEEP_STRIDE    3
#define SWEEP_STEPS     30          /* to C8, 108: the piano's range */
#define LEVEL_STEPS     8
#define LEVEL_STRIDE    16          /* velocity 127, 111 ... 15 */
#define PAN_STEPS       3

#define SILENCE_FRAMES  20
#define CLK_FRAMES      60
#define STEP_FRAMES     20

/* The resets take a while: we wait after each one. The MT-32's early
 * firmware also loses messages that come too close together */
#define RESET_WAIT_US   300000UL
#define SYSEX_WAIT_US   50000UL

uint8_t midiseq_gm_on[]  = { 0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7 };
uint8_t midiseq_gs_reset[] = { 0xF0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7F, 0x00, 0x41, 0xF7 };
uint8_t midiseq_xg_on[]  = { 0xF0, 0x43, 0x10, 0x4C, 0x00, 0x00, 0x7E, 0x00, 0xF7 };
/* MT-32 system area 10 00 03, reverb level 0. Other synths ignore a
 * message for another model */
uint8_t midiseq_mt32_reverb[] = { 0xF0, 0x41, 0x10, 0x16, 0x12, 0x10, 0x00, 0x03, 0x00, 0x6D, 0xF7 };

/* Pan for each step of the Pan block: left, right, center, the order
 * of the separation blocks of the other sequences */
uint8_t midiseq_pan[PAN_STEPS] = { 0, 127, 64 };

uint8_t midiseq_square = SQUARE_GM;

void midiseq_sysex(uint8_t *data, unsigned length, uint32_t wait_us) {
    midi_sysex(data, length);
    pit_delay_us(wait_us);
}

/* GM, GS and XG resets: each synth takes its own and ignores the
 * others. Then everything that colors the sound off: reverb, chorus,
 * XG's variation effect, modulation */
void midiseq_setup(int mt32) {
    uint8_t channel;

    midiseq_square = mt32 ? SQUARE_MT32 : SQUARE_GM;

    for (channel = 0; channel < 16; channel++)
        midi_control(channel, MIDI_CC_NOTES_OFF, 0);

    midiseq_sysex(midiseq_gm_on, sizeof(midiseq_gm_on), RESET_WAIT_US);
    midiseq_sysex(midiseq_gs_reset, sizeof(midiseq_gs_reset), RESET_WAIT_US);
    midiseq_sysex(midiseq_xg_on, sizeof(midiseq_xg_on), RESET_WAIT_US);
    midiseq_sysex(midiseq_mt32_reverb, sizeof(midiseq_mt32_reverb), SYSEX_WAIT_US);

    channel = MIDISEQ_CHANNEL;
    midi_control(channel, MIDI_CC_RESET_ALL, 0);
    midi_program(channel, midiseq_square);
    midi_control(channel, MIDI_CC_VOLUME, 100);
    midi_control(channel, MIDI_CC_EXPRESSION, 127);
    midi_control(channel, MIDI_CC_PAN, 64);
    midi_control(channel, MIDI_CC_MODULATION, 0);
    midi_control(channel, MIDI_CC_HOLD, 0);
    midi_control(channel, MIDI_CC_REVERB, 0);
    midi_control(channel, MIDI_CC_CHORUS, 0);
    midi_control(channel, MIDI_CC_VARIATION, 0);
    midi_pitch_center(channel);
    pit_delay_us(SYSEX_WAIT_US);
}

/* What a gap sets in its last frame, the note already released */
typedef enum {
    PREP_NONE,
    PREP_SOUND,         /* expression back after the pulses */
    PREP_PIANO,         /* the blocks' instrument */
    PREP_PAN,           /* the Pan block's first step */
    PREP_PULSES         /* the square again, pan centered */
} prep_t;

void midiseq_gap(unsigned frames, prep_t prep) {
    seq_silence(frames - 1);
    if (seq_stopped())
        prep = PREP_NONE;
    switch (prep) {
        case PREP_SOUND:
            midi_control(MIDISEQ_CHANNEL, MIDI_CC_EXPRESSION, 127);
            break;
        case PREP_PIANO:
            midi_program(MIDISEQ_CHANNEL, PIANO_PROGRAM);
            break;
        case PREP_PULSES:
            midi_program(MIDISEQ_CHANNEL, midiseq_square);
            midi_control(MIDISEQ_CHANNEL, MIDI_CC_PAN, 64);
            break;
        case PREP_PAN:
            midi_control(MIDISEQ_CHANNEL, MIDI_CC_PAN, midiseq_pan[0]);
            break;
        case PREP_NONE:
            break;
    }
    seq_silence(1);
}

void midiseq_wait(unsigned frames) {
    while (frames--)
        vsync_wait();
}

/* Each pulse starts with the expression up and stops with it down */
void midiseq_pulse_train() {
    int i;

    if (seq_stopped())
        return;

    seq_add_frames(PULSE_COUNT * PULSE_FRAMES * 2);
    for (i = 0; i < PULSE_COUNT; i++) {
        midi_control(MIDISEQ_CHANNEL, MIDI_CC_EXPRESSION, 127);
        midi_note_on(MIDISEQ_CHANNEL, SYNC_NOTE, 127);
        midiseq_wait(PULSE_FRAMES);
        midi_control(MIDISEQ_CHANNEL, MIDI_CC_EXPRESSION, 0);
        midi_note_off(MIDISEQ_CHANNEL, SYNC_NOTE);
        midiseq_wait(PULSE_FRAMES);
    }
}

/* One note held for the whole block, released at its end */
void midiseq_tone(uint8_t note, unsigned frames) {
    if (seq_stopped())
        return;

    midi_note_on(MIDISEQ_CHANNEL, note, TONE_VELOCITY);
    seq_silence(frames);
    midi_note_off(MIDISEQ_CHANNEL, note);
}

/* The blocks' steps, through seq_block(): on at the start, off at 80% */
uint8_t midiseq_sweep_note(int step) {
    return (uint8_t)(SWEEP_LOW + step * SWEEP_STRIDE);
}

void midiseq_sweep_on(void *context, int step) {
    midi_note_on(MIDISEQ_CHANNEL, midiseq_sweep_note(step), TONE_VELOCITY);
}

void midiseq_sweep_off(void *context, int step) {
    midi_note_off(MIDISEQ_CHANNEL, midiseq_sweep_note(step));
}

void midiseq_level_on(void *context, int step) {
    midi_note_on(MIDISEQ_CHANNEL, TONE_NOTE, (uint8_t)(127 - step * LEVEL_STRIDE));
}

void midiseq_tone_on(void *context, int step) {
    midi_note_on(MIDISEQ_CHANNEL, TONE_NOTE, TONE_VELOCITY);
}

void midiseq_tone_off(void *context, int step) {
    midi_note_off(MIDISEQ_CHANNEL, TONE_NOTE);
}

/* Pan is set with the note released, for the next step */
void midiseq_pan_prep(void *context, int step) {
    midi_control(MIDISEQ_CHANNEL, MIDI_CC_PAN, midiseq_pan[step]);
}

int midiseq_run() {
    int result;

    seq_begin(NULL);

    /* Sync */
    midiseq_pulse_train();

    /* MIDICLK */
    midiseq_gap(SILENCE_FRAMES, PREP_SOUND);
    midiseq_tone(CLOCK_NOTE, CLK_FRAMES);

    /* Sweep */
    midiseq_gap(SILENCE_FRAMES, PREP_PIANO);
    seq_block(SWEEP_STEPS, STEP_FRAMES, NULL, midiseq_sweep_on, midiseq_sweep_off, NULL);

    /* Level */
    midiseq_gap(SILENCE_FRAMES, PREP_NONE);
    seq_block(LEVEL_STEPS, STEP_FRAMES, NULL, midiseq_level_on, midiseq_tone_off, NULL);

    /* Pan */
    midiseq_gap(SILENCE_FRAMES, PREP_PAN);
    seq_block(PAN_STEPS, STEP_FRAMES, midiseq_pan_prep, midiseq_tone_on, midiseq_tone_off, NULL);

    /* Sync */
    midiseq_gap(SILENCE_FRAMES, PREP_PULSES);
    midiseq_pulse_train();

    seq_stop();
    midi_control(MIDISEQ_CHANNEL, MIDI_CC_NOTES_OFF, 0);
    midi_control(MIDISEQ_CHANNEL, MIDI_CC_EXPRESSION, 127);

    result = seq_report();
    if (midi_lost())
        printf("  WARNING: the MPU-401 didn't take %u bytes, notes may be missing\n", midi_lost());
    return result;
}
