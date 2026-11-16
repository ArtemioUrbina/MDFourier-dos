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

/* Patch Temp Area, part 1 (03 00 00), reverb switch (06h) off. A
 * program change loads the patch's own switch again. Measured: with
 * reverb on, a copy of each note came out of the other side about
 * 32 dB down; off, separation 78 dB */
uint8_t midifam_mt32_dry[] = { 0xF0, 0x41, 0x10, 0x16, 0x12, 0x03, 0x00, 0x06, 0x00, 0x77, 0xF7 };

/*
 * The MT-32 lets a note go through its release, and neither expression
 * nor volume cuts it: measured, a pulse fell about 0.65 dB per ms and
 * was still there when the next began. The tone gets a release time of
 * 0 instead, in the timbre part 1 plays (Timbre Temp Area, 04 00 00):
 * TVA ENV TIME 5, offset 35h in each of the 4 partials. A program
 * change loads the timbre from ROM again.
 */
#define MT32_PARTIALS       4
#define MT32_TVA_RELEASE    0x35

uint8_t midifam_mt32_partial[MT32_PARTIALS][2] = {
    { 0x00, 0x0E }, { 0x00, 0x48 }, { 0x01, 0x02 }, { 0x01, 0x3C }
};

/* DT1: F0 41 10 16 12, address, data, checksum, F7 */
uint8_t midifam_mt32_set[11] = { 0xF0, 0x41, 0x10, 0x16, 0x12, 0x04, 0, 0, 0, 0, 0xF7 };

void midifam_mt32_release(unsigned partial) {
    uint8_t  *m  = midifam_mt32_set;
    unsigned low = midifam_mt32_partial[partial][1] + MT32_TVA_RELEASE;

    m[6] = (uint8_t)(midifam_mt32_partial[partial][0] + (low >> 7));
    m[7] = (uint8_t)(low & 0x7F);
    m[8] = 0;
    m[9] = (uint8_t)((128 - ((m[5] + m[6] + m[7] + m[8]) & 0x7F)) & 0x7F);
    midi_sysex(m, sizeof(midifam_mt32_set));
}

void midifam_mt32_setup() {
    midifam_sysex(midifam_mt32_reverb, sizeof(midifam_mt32_reverb), MIDIFAM_SYSEX_US);
    midifam_sysex(midifam_mt32_volume, sizeof(midifam_mt32_volume), MIDIFAM_SYSEX_US);
}

/* Its effects are set by sysex, after each program change */
void midifam_mt32_effects(uint8_t channel) {
}

int midifam_mt32_bank(int set) {
    return set == MIDISET_MT32 ? MIDIFAM_NO_BANK : MIDIFAM_CANT;
}

/* The tone: release 0 on each partial, then reverb off. Other programs:
 * reverb off */
unsigned midifam_mt32_fixups(int tone) {
    return tone ? MT32_PARTIALS + 1 : 1;
}

void midifam_mt32_fixup(int tone, unsigned index) {
    if (tone && index < MT32_PARTIALS)
        midifam_mt32_release(index);
    else
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

/* Square Wave, program 47. It pans the other way round, 0 is right,
 * measured, and the SC-55 manual says so too (p. 32). It doesn't know
 * sound off (120): the sequence waits */
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
