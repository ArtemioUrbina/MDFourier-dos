#include <string.h>
#include <ctype.h>
#include "midifam.h"
#include "midiset.h"
#include "midi.h"
#include "pit.h"

/* The MIDI channel everything plays on: 2, the MT-32's first part.
 * GM, GS and XG synths play every channel */
#define MIDIFAM_CHANNEL     1

void midifam_sysex(uint8_t *data, unsigned length, uint32_t wait_us) {
    midi_sysex(data, length);
    pit_delay_us(wait_us);
}

/* Reverb and chorus to 0 by controller: GM, GS and XG */
void midifam_effects_cc(uint8_t channel) {
    midi_control(channel, MIDI_CC_REVERB, 0);
    midi_control(channel, MIDI_CC_CHORUS, 0);
}

/* ---- GM ---- */

uint8_t midifam_gm_on[]     = { 0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7 };
/* GM Master Volume, universal, 14 bits, 3FFFh: at its top whatever the
 * front panel or a game left */
uint8_t midifam_gm_volume[] = { 0xF0, 0x7F, 0x7F, 0x04, 0x01, 0x7F, 0x7F, 0xF7 };

void midifam_gm_setup() {
    midifam_sysex(midifam_gm_on, sizeof(midifam_gm_on), MIDIFAM_RESET_US);
    midifam_sysex(midifam_gm_volume, sizeof(midifam_gm_volume), MIDIFAM_SYSEX_US);
}

/* Channel 10 plays the standard kit after the reset */
void midifam_no_drum_setup(int set) {
}

/* GM has one bank */
int midifam_gm_bank(int set) {
    return set == MIDISET_GM ? MIDIFAM_NO_BANK : MIDIFAM_CANT;
}

/* Lead 1 (square), the plainest tone GM has. FluidSynth plays it */
midi_family_t midifam_gm = {
    "GM", "GM", MIDISET_GM, MIDIFAM_NO_BANK, 80, 0, 127, MIDIFAM_PULSE_EXPRESSION, 1,
    midifam_gm_setup, midifam_effects_cc, midifam_gm_bank, NULL, NULL,
    midifam_no_drum_setup
};

/* ---- GS ---- */

uint8_t midifam_gs_reset[]  = { 0xF0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7F, 0x00, 0x41, 0xF7 };
/* GS master volume, 40 00 04: the SC-55 predates GM's message */
uint8_t midifam_gs_volume[] = { 0xF0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x04, 0x7F, 0x3D, 0xF7 };

void midifam_gs_setup() {
    midifam_sysex(midifam_gs_reset, sizeof(midifam_gs_reset), MIDIFAM_RESET_US);
    midifam_sysex(midifam_gs_volume, sizeof(midifam_gs_volume), MIDIFAM_SYSEX_US);
}

/* The MT-32 set through the GS MT-32 map, bank 127 (SC-55 manual
 * p. 69), and its drums, the CM-64/32L set, program 127 (p. 21, 31) */
int midifam_gs_bank(int set) {
    return set == MIDISET_MT32 ? 127 : 0;
}

void midifam_gs_drum_setup(int set) {
    midi_program(MIDIFAM_DRUMS, set == MIDISET_MT32 ? 127 : 0);
}

/*
 * The tone is bank 8 of program 80, the Sine Wave on GS synths: one
 * oscillator. The SC-55's manual doesn't list it, but on an SC-55 its
 * spectrum is a single peak (measured). The Square Wave is two, about
 * 5 Hz apart at A5: the tuning read one of them and at C8 their
 * beating broke the pulses.
 * With the sine an SC-55 synced in two takes, tuning 0.2 ppm apart,
 * noise floor -88 dBr with sound off ending the last note.
 *
 * Its pulses end with sound off (120). Measured on an SC-55, the
 * expression at 0 ended them 0 to 21 ms late, varying pulse to pulse
 * (28.5 to 50 ms for 28.5), while each pulse started on time to 0.2 ms:
 * one train in four wasn't found without -TTTT. The MT-32's, ended by
 * its release of 0, last 27.5 to 28.3 ms
 */
midi_family_t midifam_gs = {
    "GS", "GS", MIDISET_GM, 8, 80, 0, 127, MIDIFAM_PULSE_SOUND_OFF, 1,
    midifam_gs_setup, midifam_effects_cc, midifam_gs_bank, NULL, NULL,
    midifam_gs_drum_setup
};

/* ---- XG, as GM with its own reset until a Yamaha is measured ---- */

uint8_t midifam_xg_on[] = { 0xF0, 0x43, 0x10, 0x4C, 0x00, 0x00, 0x7E, 0x00, 0xF7 };

void midifam_xg_setup() {
    midifam_sysex(midifam_xg_on, sizeof(midifam_xg_on), MIDIFAM_RESET_US);
    midifam_sysex(midifam_gm_volume, sizeof(midifam_gm_volume), MIDIFAM_SYSEX_US);
}

/* And the variation effect */
void midifam_xg_effects(uint8_t channel) {
    midifam_effects_cc(channel);
    midi_control(channel, MIDI_CC_VARIATION, 0);
}

int midifam_xg_bank(int set) {
    return set == MIDISET_GM ? 0 : MIDIFAM_CANT;
}

midi_family_t midifam_xg = {
    "XG", "XG", MIDISET_GM, MIDIFAM_NO_BANK, 80, 0, 127, MIDIFAM_PULSE_EXPRESSION, 1,
    midifam_xg_setup, midifam_xg_effects, midifam_xg_bank, NULL, NULL,
    midifam_no_drum_setup
};

/* ---- MT-32, CM-32L, LAPC-I ---- */

/*
 * Source: Roland MT-32 MIDI Implementation, 1987. Model 16h, DT1 12h.
 * System area 10 00 03, reverb level 0: the lowest of 8, not off.
 * System area 10 00 16, master volume 100, the one the panel changes.
 */
uint8_t midifam_mt32_reverb[] = { 0xF0, 0x41, 0x10, 0x16, 0x12, 0x10, 0x00, 0x03, 0x00, 0x6D, 0xF7 };
uint8_t midifam_mt32_volume[] = { 0xF0, 0x41, 0x10, 0x16, 0x12, 0x10, 0x00, 0x16, 0x64, 0x76, 0xF7 };

/* All parameter reset, address 7F xx xx in the same document, sent
 * first. Measured: without it, back to back takes on an MT-32 played
 * Organ and Brass in one of two ways; with it, four takes all played
 * them the same way, and the other instruments compared better too.
 * The wait is the one the other resets get, it worked, the shortest
 * the MT-32 needs is not measured */
uint8_t midifam_mt32_reset[]  = { 0xF0, 0x41, 0x10, 0x16, 0x12, 0x7F, 0x00, 0x00, 0x00, 0x01, 0xF7 };

/* Patch Temp Area, part 1 (03 00 00), reverb switch (06h) off. A
 * program change loads the patch's own switch again. Measured: with
 * reverb on, a copy of each note came out of the other side about
 * 32 dB down; off, separation 78 dB */
uint8_t midifam_mt32_dry[] = { 0xF0, 0x41, 0x10, 0x16, 0x12, 0x03, 0x00, 0x06, 0x00, 0x77, 0xF7 };

/*
 * Our own tone, loaded the way MT-32 games load theirs: into Timbre
 * Memory 1 (08 00 00), the common parameters and partial 1, and Patch
 * Memory 48 (05 02 78) pointed at it, so program 47 plays it. Both
 * at setup, before the timing starts, with a wait after each: we have
 * no early firmware MT-32 to measure, and this is the path games used
 * on all of them. Nothing for the tone goes out during the sequence.
 *
 * The ROM tone (program 47) is two or more oscillators a few cents
 * apart. Measured: at A5 lines at 881.7, 884.4 and 886.8 Hz, its level
 * beating down 14 dB about every 27 frames, the ToneLevel steps
 * 13 dB down by frame 24. Holding its TVA levels at 100 changed
 * nothing. Ours is one partial: an LA square, no pitch envelope nor
 * LFO, the filter open, the level held, and a release of 0. The MT-32
 * lets a note go through its release, and neither expression nor
 * volume cuts it: measured, a pulse fell about 0.65 dB per ms and was
 * still there when the next began, so the note off has to end it.
 *
 * Partial layout and neutral values from munt (mt32emu): coarse 36 and
 * fine 50 no transposition, keyfollow 11 is 1, pitch envelope levels 50
 * no change, TVA velocity sensitivity 50 none, TVA bias level 12 and
 * TVF bias level 7 no change, pulse width velocity sensitivity 7 none.
 * The TVF keyfollow matches the pitch one so the cutoff doesn't move
 * with the key. The TVA velocity sensitivity, 75, sets the ToneLevel
 * steps: measured, 8.3 dB down at velocity 71, in steps of 1.1 dB, a GS
 * sine went 10 dB down.
 *
 * Measured, two takes: one line, 883.83 Hz at A5, the level held
 * within 0.1 dB to the note off, the takes the same within 0.1 dB.
 */
uint8_t midifam_mt32_tone[] = {
    0xF0, 0x41, 0x10, 0x16, 0x12, 0x08, 0x00, 0x00,
    /* Common: name, structure 1&2 and 3&4 both synth, partial 1 only,
     * sustain on */
    'M', 'D', 'F', ' ', 'T', 'o', 'n', 'e', ' ', ' ', 0, 0, 0x01, 0,
    /* WG: coarse, fine, keyfollow, bender off, square, PCM wave,
     * pulse width 50%, its velocity sensitivity */
    36, 50, 11, 0, 0, 0, 0, 7,
    /* Pitch envelope: depth 0, velocity, time keyfollow, 4 times,
     * 5 levels */
    0, 0, 0, 0, 0, 0, 0, 50, 50, 50, 50, 50,
    /* Pitch LFO: rate, depth, modulation sensitivity */
    0, 0, 0,
    /* TVF: cutoff open, resonance, keyfollow, bias point and level,
     * envelope depth 0, velocity, depth and time keyfollow, 5 times,
     * 4 levels */
    100, 0, 11, 64, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 100, 100, 100,
    /* TVA: level, velocity sensitivity, bias points and levels, time
     * keyfollow and velocity, 5 times (attack and release 0), 4 levels */
    100, 75, 64, 12, 64, 12, 0, 0, 0, 0, 0, 0, 0, 100, 100, 100, 100,
    0, 0xF7
};

/* Patch Memory: timbre group 2 (memory), timbre 1, key shift 24 and
 * fine tune 50 none, bender range 12, assign mode 0, reverb off */
uint8_t midifam_mt32_patch[] = {
    0xF0, 0x41, 0x10, 0x16, 0x12, 0x05, 0x02, 0x78,
    2, 0, 24, 50, 12, 0, 0, 0,
    0, 0xF7
};

/* Fills in a DT1's checksum: address and data, before the F7 */
void midifam_roland_checksum(uint8_t *m, unsigned length) {
    unsigned i, sum = 0;

    for (i = 5; i < length - 2; i++)
        sum += m[i];
    m[length - 2] = (uint8_t)((128 - (sum & 0x7F)) & 0x7F);
}

/* Generous, the most we wait for anything else, not measured */
#define MT32_MEMORY_US  MIDIFAM_RESET_US

void midifam_mt32_setup() {
    midifam_sysex(midifam_mt32_reset, sizeof(midifam_mt32_reset), MIDIFAM_RESET_US);
    midifam_sysex(midifam_mt32_reverb, sizeof(midifam_mt32_reverb), MIDIFAM_SYSEX_US);
    midifam_sysex(midifam_mt32_volume, sizeof(midifam_mt32_volume), MIDIFAM_SYSEX_US);
    midifam_roland_checksum(midifam_mt32_tone, sizeof(midifam_mt32_tone));
    midifam_sysex(midifam_mt32_tone, sizeof(midifam_mt32_tone), MT32_MEMORY_US);
    midifam_roland_checksum(midifam_mt32_patch, sizeof(midifam_mt32_patch));
    midifam_sysex(midifam_mt32_patch, sizeof(midifam_mt32_patch), MT32_MEMORY_US);
}

/* Its effects are set by sysex, after each program change */
void midifam_mt32_effects(uint8_t channel) {
}

int midifam_mt32_bank(int set) {
    return set == MIDISET_MT32 ? MIDIFAM_NO_BANK : MIDIFAM_CANT;
}

/* The tone's patch has reverb off. Other programs load their patch's
 * own switch: reverb off after each */
unsigned midifam_mt32_fixups(int tone) {
    return tone ? 0 : 1;
}

void midifam_mt32_fixup(int tone, unsigned index) {
    midi_sysex(midifam_mt32_dry, sizeof(midifam_mt32_dry));
}

/* The rhythm part's keys each have their own reverb switch, Rhythm
 * Setup Temp Area (03 01 10), 4 bytes a key from key 24, the switch at
 * 3. Off for the keys the set plays: from the manual, as the parts'
 * reverb, which we measured */
uint8_t midifam_mt32_key[11] = { 0xF0, 0x41, 0x10, 0x16, 0x12, 0x03, 0, 0, 0, 0, 0xF7 };

void midifam_mt32_drum_setup(int set) {
    unsigned i, address;
    uint8_t  *m = midifam_mt32_key;

    for (i = 0; i < MIDISET_DRUMS; i++) {
        address = (0x01 << 7) + 0x10 + (midi_sets[set].drums[i] - 24) * 4 + 3;
        m[6] = (uint8_t)(address >> 7);
        m[7] = (uint8_t)(address & 0x7F);
        m[8] = 0;
        m[9] = (uint8_t)((128 - ((m[5] + m[6] + m[7] + m[8]) & 0x7F)) & 0x7F);
        midifam_sysex(m, sizeof(midifam_mt32_key), MIDIFAM_SYSEX_US);
    }
}

/* Program 47, our tone in its patch. It pans the other way round,
 * 0 is right, measured, and the SC-55 manual says so too (p. 32). It
 * doesn't know sound off (120): the sequence waits */
midi_family_t midifam_mt32 = {
    "MT-32", "MT32", MIDISET_MT32, MIDIFAM_NO_BANK, 47, 127, 0, MIDIFAM_PULSE_NOTE_OFF, 0,
    midifam_mt32_setup, midifam_mt32_effects, midifam_mt32_bank,
    midifam_mt32_fixups, midifam_mt32_fixup, midifam_mt32_drum_setup
};

midi_family_t *midi_families[MIDIFAM_COUNT] = {
    &midifam_gm, &midifam_gs, &midifam_xg, &midifam_mt32
};

midi_family_t *midifam_find(char *option) {
    int i;

    for (i = 0; i < MIDIFAM_COUNT; i++)
        if (!stricmp(option, midi_families[i]->option))
            return midi_families[i];
    return NULL;
}
