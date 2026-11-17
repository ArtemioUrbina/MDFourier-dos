#include <stdio.h>
#include "midiseq.h"
#include "midiset.h"
#include "midi.h"
#include "seq.h"
#include "vsync.h"
#include "pit.h"

/*
 * MIDI sequence (/M). How we talk to a synth is in MIDIFAM.C, what the
 * instrument blocks play in MIDISET.C. Notes on channel 2, drums on 10.
 *
 *   Sync        10 pulses of the tone at C8, one frame on, one off
 *   Padding, Silence
 *   MIDICLK     the tone at A5, the tuning
 *   ToneSweep   30 notes, A0 to C8
 *   ToneLevel   A4, velocity 127 down to 71
 *   TonePan     A4 left, right, center
 *   8 instruments, 8 notes each
 *   CustomCLK   the clock timbre, fixed at C4, MT-32 set
 *   5 games' custom timbres, MT-32 set
 *   Drums       8 hits
 *   Padding, Silence, Sync
 *
 * C8 is the highest note every synth plays, the profile's 6000 Hz finds
 * it. Pulses are one frame: with two, MDFourier placed an MT-32 train
 * one pulse late.
 */

#define MIDISEQ_CHANNEL 1           /* MIDI channel 2 */
#define MIDI_CC_BANK    0
#define TONE            -1
#define CUSTOM_CLOCK    -2          /* the set's custom clock, MT-32 set */

#define PULSE_COUNT     10
#define PULSE_FRAMES    1           /* on, then as many off */
#define SYNC_NOTE       108         /* C8, 4186.0 Hz */
#define CLOCK_NOTE      81          /* A5, 880 Hz */
#define TONE_NOTE       69          /* A4, 440 Hz */
#define VELOCITY        127         /* as loud as the synth plays */

#define SWEEP_LOW       21          /* A0 */
#define SWEEP_STRIDE    3
#define SWEEP_STEPS     30          /* to C8, 108 */
#define LEVEL_STEPS     8
/* Velocity 127, 119 ... 71, steps of 16 ended under the MT-32's noise */
#define LEVEL_STRIDE    8
#define PAN_STEPS       3

#define SILENCE_FRAMES  20
/* Last release: an MT-32 piano A4 was still at -38 dBr after 20 */
#define TAIL_FRAMES     60
#define CLK_FRAMES      60
#define SWEEP_FRAMES    10
#define FIXUP_FRAMES    2

/* Note step. 40 compared as well or better than 20 on an MT-32 and
 * SC-55 (measured), the profiles read part of it per block */
#define STEP_FRAMES     40

midi_family_t *midiseq_family;
midi_set_t    *midiseq_set;
int           midiseq_set_index;
uint8_t       midiseq_pan[PAN_STEPS];   /* left, right, center */

/* An instrument, or past MIDISET_INSTRUMENTS one of the set's customs */
midi_instrument_t *midiseq_instrument_of(int instrument) {
    if (instrument == CUSTOM_CLOCK)
        return &midiseq_set->custom_clock->instrument;
    if (instrument >= MIDISET_INSTRUMENTS)
        return &midiseq_set->customs[instrument - MIDISET_INSTRUMENTS].instrument;
    return &midiseq_set->instruments[instrument];
}

/* The tone or an instrument, with its bank */
void midiseq_select(int instrument) {
    int bank = instrument == TONE ? midiseq_family->tone_bank
                                  : midiseq_family->set_bank(midiseq_set_index);

    if (bank >= 0)
        midi_control(MIDISEQ_CHANNEL, MIDI_CC_BANK, (uint8_t)bank);
    midi_program(MIDISEQ_CHANNEL, instrument == TONE ? midiseq_family->tone_program
                                                     : midiseq_instrument_of(instrument)->program);
}

unsigned midiseq_fixups(int instrument) {
    return midiseq_family->fixups ? midiseq_family->fixups(instrument == TONE) : 0;
}

/* Effects off, centered, full level */
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
    unsigned fixup;

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
    if (family->custom_load && midiseq_set->custom_count)
        family->custom_load(midiseq_set->customs, midiseq_set->custom_count,
                            midiseq_set->custom_clock);

    midiseq_select(TONE);
    for (fixup = 0; fixup < midiseq_fixups(TONE); fixup++) {
        pit_delay_us(MIDIFAM_SYSEX_US);
        family->fixup(1, fixup);
    }
    pit_delay_us(MIDIFAM_SYSEX_US);
}

/* The last release goes in the padding. Sound off (120) where known,
 * notes off (123) on the MT-32, so its padding is longer. Volume and
 * expression at 0 didn't stop an MT-32 release, and threw the end
 * sync 12 ms late (measured) */
void midiseq_sound_off(uint8_t channel) {
    if (seq_stopped())
        return;
    if (midiseq_family->sound_off)
        midi_control(channel, MIDI_CC_SOUND_OFF, 0);
    midi_control(channel, MIDI_CC_NOTES_OFF, 0);
    if (midiseq_family->pulse_end == MIDIFAM_PULSE_EXPRESSION)
        midi_control(channel, MIDI_CC_EXPRESSION, 127);
}

/* Silence, the TonePan pan set in its last frame */
void midiseq_gap(unsigned frames, int tonepan_next) {
    seq_silence(frames - 1);
    if (tonepan_next && !seq_stopped())
        midi_control(MIDISEQ_CHANNEL, MIDI_CC_PAN, midiseq_pan[0]);
    seq_silence(1);
}

/* Program change at the start of a gap, then the fixups every
 * FIXUP_FRAMES, the MT-32's early firmware needs the time */
void midiseq_program_gap(unsigned frames, int instrument) {
    unsigned fixup;

    if (!seq_stopped()) {
        midiseq_select(instrument);
        midi_control(MIDISEQ_CHANNEL, MIDI_CC_PAN, 64);
    }
    seq_silence(FIXUP_FRAMES);
    frames -= FIXUP_FRAMES;

    for (fixup = 0; fixup < midiseq_fixups(instrument); fixup++) {
        if (!seq_stopped())
            midiseq_family->fixup(instrument == TONE, fixup);
        seq_silence(FIXUP_FRAMES);
        frames -= FIXUP_FRAMES;
    }
    seq_silence(frames);
}

void midiseq_wait_frames(unsigned frames) {
    while (frames--)
        vsync_wait();
}

/* Each family ends a pulse its own way, the note on starts it */
void midiseq_pulse_train() {
    int pulse;

    if (seq_stopped())
        return;

    seq_add_frames(PULSE_COUNT * PULSE_FRAMES * 2);
    for (pulse = 0; pulse < PULSE_COUNT; pulse++) {
        if (midiseq_family->pulse_end == MIDIFAM_PULSE_EXPRESSION)
            midi_control(MIDISEQ_CHANNEL, MIDI_CC_EXPRESSION, 127);
        midi_note_on(MIDISEQ_CHANNEL, SYNC_NOTE, VELOCITY);
        midiseq_wait_frames(PULSE_FRAMES);
        if (midiseq_family->pulse_end == MIDIFAM_PULSE_EXPRESSION)
            midi_control(MIDISEQ_CHANNEL, MIDI_CC_EXPRESSION, 0);
        else if (midiseq_family->pulse_end == MIDIFAM_PULSE_SOUND_OFF)
            midi_control(MIDISEQ_CHANNEL, MIDI_CC_SOUND_OFF, 0);
        midi_note_off(MIDISEQ_CHANNEL, SYNC_NOTE);
        midiseq_wait_frames(PULSE_FRAMES);
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

/* Block steps for seq_block(): on at the start, off at 80% */
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

void midiseq_tone_a4_on(void *context, int step) {
    midi_note_on(MIDISEQ_CHANNEL, TONE_NOTE, VELOCITY);
}

void midiseq_tone_a4_off(void *context, int step) {
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
    int instrument, custom, result;

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
    seq_block(LEVEL_STEPS, STEP_FRAMES, NULL, midiseq_level_on, midiseq_tone_a4_off, NULL);
    midiseq_gap(SILENCE_FRAMES, 1);
    seq_block(PAN_STEPS, STEP_FRAMES, midiseq_pan_prep, midiseq_tone_a4_on, midiseq_tone_a4_off, NULL);

    /* Typical: each instrument after a gap that selects it */
    for (instrument = 0; instrument < MIDISET_INSTRUMENTS; instrument++) {
        midiseq_program_gap(SILENCE_FRAMES, instrument);
        seq_block(MIDISET_NOTES, STEP_FRAMES, NULL, midiseq_note_on, midiseq_note_off,
                  &midiseq_set->instruments[instrument]);
    }

    /* The games' custom timbres, MT-32 set, after the clock that tells
     * MDFourier they were loaded. Before the drums, their crash rings
     * into what follows */
    if (midiseq_set->custom_clock) {
        midiseq_program_gap(SILENCE_FRAMES, CUSTOM_CLOCK);
        midiseq_tone(midiseq_set->custom_clock->instrument.low, CLK_FRAMES);
    }
    for (custom = 0; custom < midiseq_set->custom_count; custom++) {
        midiseq_program_gap(SILENCE_FRAMES, MIDISET_INSTRUMENTS + custom);
        seq_block(MIDISET_NOTES, STEP_FRAMES, NULL, midiseq_note_on, midiseq_note_off,
                  &midiseq_set->customs[custom].instrument);
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
