#include <stdio.h>
#include "midiseq.h"
#include "midiset.h"
#include "midi.h"
#include "seq.h"
#include "vsync.h"
#include "pit.h"

/*
 * MIDI sequence (/M): the timeline. How each synth is talked to is its
 * family's (MIDIFAM.C), what the typical blocks play is the content
 * set's (MIDISET.C). Melodic notes on MIDI channel 2, drums on 10.
 *
 *   Sync        10 pulses of the tone at C8 (4186 Hz), two frames on, two off
 *   Padding     the pulses' release, out of the silence
 *   Silence
 *   MIDICLK     the tone at A5 (880 Hz), the tuning: MDFourier reads A4
 *
 *   Clean, the family's tone, the synth's own engine:
 *   ToneSweep   30 notes, A0 to C8 in minor thirds
 *   ToneLevel   A4, velocity 127 down to 71
 *   TonePan     A4 left, right, center
 *
 *   Typical, the set's instruments, what games played:
 *   8 instruments, 8 notes each across their range
 *   Drums       8 hits
 *
 *   Padding     the last release, out of the silence
 *   Silence
 *   Sync
 *
 * MDFourier finds the pulses in windows of a fraction of a millisecond,
 * where a tone near 6 kHz stands out best. C8 is the highest note every
 * synth plays (FluidSynth plays nothing above it with the soundfonts we
 * tried, the MT-32 plays 12 to 108); the profile asks for 6000 Hz, which
 * in those windows finds C8 too. A synth's attack and release blur a
 * pulse of one frame, so ours are two, and the profile says so (28 ms,
 * Sync blocks of 40 frames).
 *
 * The clean blocks compare a synth with others of its family, real or
 * emulated. The typical blocks compare any two synths that played the
 * same set.
 */

#define MIDISEQ_CHANNEL 1           /* MIDI channel 2 */
#define MIDI_CC_BANK    0
#define TONE            -1          /* midiseq_instrument(): the family's tone */

#define PULSE_COUNT     10
#define PULSE_FRAMES    2           /* on, then as many off */
#define SYNC_NOTE       108         /* C8, 4186.0 Hz */
#define CLOCK_NOTE      81          /* A5, 880 Hz */
#define TONE_NOTE       69          /* A4, 440 Hz */
#define VELOCITY        127         /* as loud as the synth plays */

#define SWEEP_LOW       21          /* A0 */
#define SWEEP_STRIDE    3
#define SWEEP_STEPS     30          /* to C8, 108 */
#define LEVEL_STEPS     8
/* Velocity 127, 119 ... 71. Steps of 16 went down 26 dB on an MT-32,
 * the last two under its noise */
#define LEVEL_STRIDE    8
#define PAN_STEPS       3

#define SILENCE_FRAMES  20
/* After the last note, before the Silence: measured on an MT-32, its
 * piano A4 fell about 75 dB a second and then about 40, and nothing
 * stops a note releasing there. 20 frames left it at -38 dBr */
#define TAIL_FRAMES     60
#define CLK_FRAMES      60
#define SWEEP_FRAMES    10
#define STEP_FRAMES     20
#define FIXUP_FRAMES    2           /* between messages after a program change */

midi_family_t *midiseq_family;
midi_set_t    *midiseq_set;
int           midiseq_set_index;
uint8_t       midiseq_pan[PAN_STEPS];   /* left, right, center */

/* The tone, or one of the set's instruments, with the bank select each
 * needs */
void midiseq_instrument(int which) {
    int bank = which == TONE ? midiseq_family->tone_bank
                             : midiseq_family->set_bank(midiseq_set_index);

    if (bank >= 0)
        midi_control(MIDISEQ_CHANNEL, MIDI_CC_BANK, (uint8_t)bank);
    midi_program(MIDISEQ_CHANNEL, which == TONE ? midiseq_family->tone_program
                                                : midiseq_set->instruments[which].program);
}

unsigned midiseq_fixups(int which) {
    return midiseq_family->fixups ? midiseq_family->fixups(which == TONE) : 0;
}

/* Everything that colors the sound off, centered, at full level */
void midiseq_channel_setup(uint8_t channel) {
    midi_control(channel, MIDI_CC_RESET_ALL, 0);
    midi_control(channel, MIDI_CC_VOLUME, 127);
    midi_control(channel, MIDI_CC_EXPRESSION, 127);
    midi_control(channel, MIDI_CC_PAN, 64);
    midi_control(channel, MIDI_CC_MODULATION, 0);
    midi_control(channel, MIDI_CC_HOLD, 0);
    midiseq_family->effects_off(channel);
    midi_pitch_center(channel);
}

void midiseq_setup(midi_family_t *family, int set) {
    uint8_t  channel;
    unsigned i;

    midiseq_family    = family;
    midiseq_set_index = set;
    midiseq_set       = &midi_sets[set];
    midiseq_pan[0]    = family->pan_left;
    midiseq_pan[1]    = family->pan_right;
    midiseq_pan[2]    = 64;

    for (channel = 0; channel < 16; channel++)
        midi_control(channel, MIDI_CC_NOTES_OFF, 0);
    family->setup();

    midiseq_channel_setup(MIDISEQ_CHANNEL);
    midiseq_channel_setup(MIDIFAM_DRUMS);
    family->drum_setup(set);

    midiseq_instrument(TONE);
    for (i = 0; i < midiseq_fixups(TONE); i++) {
        pit_delay_us(MIDIFAM_SYSEX_US);
        family->fixup(1, i);
    }
    pit_delay_us(MIDIFAM_SYSEX_US);
}

/* A release ends in a padding, out of the Silence blocks MDFourier
 * reads as the noise floor. Sound off (120) cuts it where the synth
 * knows it, notes off (123) releases it on the MT-32, the padding
 * before the closing Silence is longer for it (TAIL_FRAMES). The
 * expression the pulses left at 0 comes back here.
 *
 * Volume and expression at 0 through the paddings didn't stop a note
 * releasing on the MT-32 (measured), and its first pulse after them
 * came in quieter on one side: the end sync was found 12 ms late and
 * the frame rate read 650 ppm low. We don't use them */
void midiseq_sound_off(uint8_t channel) {
    if (seq_stopped())
        return;
    if (midiseq_family->sound_off)
        midi_control(channel, MIDI_CC_SOUND_OFF, 0);
    midi_control(channel, MIDI_CC_NOTES_OFF, 0);
    if (midiseq_family->pulse_end == MIDIFAM_PULSE_EXPRESSION)
        midi_control(channel, MIDI_CC_EXPRESSION, 127);
}

/* frames of silence, the pan for the TonePan block in the last one */
void midiseq_gap(unsigned frames, int pan_first) {
    seq_silence(frames - 1);
    if (pan_first && !seq_stopped())
        midi_control(MIDISEQ_CHANNEL, MIDI_CC_PAN, midiseq_pan[0]);
    seq_silence(1);
}

/* A gap that changes the instrument at its start, so whatever the
 * change cuts of the last note stays in the gap, and sends what the
 * family needs after it one message every FIXUP_FRAMES: time for the
 * MT-32's early firmware to take each */
void midiseq_program_gap(unsigned frames, int which) {
    unsigned i;

    if (!seq_stopped()) {
        midiseq_instrument(which);
        midi_control(MIDISEQ_CHANNEL, MIDI_CC_PAN, 64);
    }
    seq_silence(FIXUP_FRAMES);
    frames -= FIXUP_FRAMES;

    for (i = 0; i < midiseq_fixups(which); i++) {
        if (!seq_stopped())
            midiseq_family->fixup(which == TONE, i);
        seq_silence(FIXUP_FRAMES);
        frames -= FIXUP_FRAMES;
    }
    seq_silence(frames);
}

void midiseq_wait(unsigned frames) {
    while (frames--)
        vsync_wait();
}

/* A note off lets the note ring on through its release, longer than the
 * frames between pulses: each family ends a pulse its own way, sound off,
 * the expression, or on the MT-32 the tone's release of 0. The pulse's
 * end is in the synth's hands, its start isn't: note on comes first */
void midiseq_pulse_train() {
    int i;

    if (seq_stopped())
        return;

    seq_add_frames(PULSE_COUNT * PULSE_FRAMES * 2);
    for (i = 0; i < PULSE_COUNT; i++) {
        if (midiseq_family->pulse_end == MIDIFAM_PULSE_EXPRESSION)
            midi_control(MIDISEQ_CHANNEL, MIDI_CC_EXPRESSION, 127);
        midi_note_on(MIDISEQ_CHANNEL, SYNC_NOTE, VELOCITY);
        midiseq_wait(PULSE_FRAMES);
        if (midiseq_family->pulse_end == MIDIFAM_PULSE_EXPRESSION)
            midi_control(MIDISEQ_CHANNEL, MIDI_CC_EXPRESSION, 0);
        else if (midiseq_family->pulse_end == MIDIFAM_PULSE_SOUND_OFF)
            midi_control(MIDISEQ_CHANNEL, MIDI_CC_SOUND_OFF, 0);
        midi_note_off(MIDISEQ_CHANNEL, SYNC_NOTE);
        midiseq_wait(PULSE_FRAMES);
    }
}

/* One note held for the whole block, released at its end */
void midiseq_tone(uint8_t note, unsigned frames) {
    if (seq_stopped())
        return;

    midi_note_on(MIDISEQ_CHANNEL, note, VELOCITY);
    seq_silence(frames);
    midi_note_off(MIDISEQ_CHANNEL, note);
}

/* The blocks' steps, through seq_block(): on at the start, off at 80% */
uint8_t midiseq_sweep_note(int step) {
    return (uint8_t)(SWEEP_LOW + step * SWEEP_STRIDE);
}

void midiseq_sweep_on(void *context, int step) {
    midi_note_on(MIDISEQ_CHANNEL, midiseq_sweep_note(step), VELOCITY);
}

void midiseq_sweep_off(void *context, int step) {
    midi_note_off(MIDISEQ_CHANNEL, midiseq_sweep_note(step));
}

void midiseq_level_on(void *context, int step) {
    midi_note_on(MIDISEQ_CHANNEL, TONE_NOTE, (uint8_t)(127 - step * LEVEL_STRIDE));
}

void midiseq_a4_on(void *context, int step) {
    midi_note_on(MIDISEQ_CHANNEL, TONE_NOTE, VELOCITY);
}

void midiseq_a4_off(void *context, int step) {
    midi_note_off(MIDISEQ_CHANNEL, TONE_NOTE);
}

/* Pan is set with the note released, for the next step */
void midiseq_pan_prep(void *context, int step) {
    midi_control(MIDISEQ_CHANNEL, MIDI_CC_PAN, midiseq_pan[step]);
}

/* context: the instrument */
void midiseq_note_on(void *context, int step) {
    midi_note_on(MIDISEQ_CHANNEL, midiset_note((midi_instrument_t *)context, step), VELOCITY);
}

void midiseq_note_off(void *context, int step) {
    midi_note_off(MIDISEQ_CHANNEL, midiset_note((midi_instrument_t *)context, step));
}

void midiseq_drum_on(void *context, int step) {
    midi_note_on(MIDIFAM_DRUMS, midiseq_set->drums[step], VELOCITY);
}

void midiseq_drum_off(void *context, int step) {
    midi_note_off(MIDIFAM_DRUMS, midiseq_set->drums[step]);
}

int midiseq_run() {
    int i, result;

    seq_begin(NULL);

    /* Sync, Padding */
    midiseq_pulse_train();
    midiseq_sound_off(MIDISEQ_CHANNEL);
    seq_silence(SILENCE_FRAMES);

    /* Silence, MIDICLK */
    midiseq_gap(SILENCE_FRAMES, 0);
    midiseq_tone(CLOCK_NOTE, CLK_FRAMES);

    /* Clean: ToneSweep, ToneLevel, TonePan */
    midiseq_gap(SILENCE_FRAMES, 0);
    seq_block(SWEEP_STEPS, SWEEP_FRAMES, NULL, midiseq_sweep_on, midiseq_sweep_off, NULL);
    midiseq_gap(SILENCE_FRAMES, 0);
    seq_block(LEVEL_STEPS, STEP_FRAMES, NULL, midiseq_level_on, midiseq_a4_off, NULL);
    midiseq_gap(SILENCE_FRAMES, 1);
    seq_block(PAN_STEPS, STEP_FRAMES, midiseq_pan_prep, midiseq_a4_on, midiseq_a4_off, NULL);

    /* Typical: each instrument after a gap that selects it */
    for (i = 0; i < MIDISET_INSTRUMENTS; i++) {
        midiseq_program_gap(SILENCE_FRAMES, i);
        seq_block(MIDISET_NOTES, STEP_FRAMES, NULL, midiseq_note_on, midiseq_note_off,
                  &midiseq_set->instruments[i]);
    }

    /* Drums */
    midiseq_gap(SILENCE_FRAMES, 0);
    seq_block(MIDISET_DRUMS, STEP_FRAMES, NULL, midiseq_drum_on, midiseq_drum_off, NULL);

    /* Padding, longer: the last release */
    midiseq_sound_off(MIDISEQ_CHANNEL);
    midiseq_sound_off(MIDIFAM_DRUMS);
    midiseq_gap(TAIL_FRAMES, 0);

    /* Silence with the tone back, Sync */
    midiseq_program_gap(SILENCE_FRAMES, TONE);
    midiseq_pulse_train();

    seq_stop();
    midi_control(MIDISEQ_CHANNEL, MIDI_CC_NOTES_OFF, 0);
    midi_control(MIDISEQ_CHANNEL, MIDI_CC_EXPRESSION, 127);

    result = seq_report();
    if (midi_lost())
        printf("  WARNING: the MPU-401 didn't take %u bytes, notes may be missing\n", midi_lost());
    return result;
}
