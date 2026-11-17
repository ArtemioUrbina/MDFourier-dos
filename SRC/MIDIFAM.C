#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "midifam.h"
#include "midiset.h"
#include "midi.h"
#include "pit.h"

/* Channel 2, the MT-32's first part, GM/GS/XG play any */
#define MIDIFAM_CHANNEL     1

void midifam_sysex(uint8_t *data, unsigned length, uint32_t wait_us) {
    midi_sysex(data, length);
    pit_delay_us(wait_us);
}

/* Reverb and chorus to 0, GM, GS and XG */
void midifam_effects_off_cc(uint8_t channel) {
    midi_control(channel, MIDI_CC_REVERB, 0);
    midi_control(channel, MIDI_CC_CHORUS, 0);
}

/* ---- GM ---- */

uint8_t midifam_gm_on[]     = { 0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7 };
/* GM Master Volume at its top, whatever the panel or a game left */
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

/* Lead 1 (square), the plainest GM tone */
midi_family_t midifam_gm = {
    "GM", "GM", MIDISET_GM, MIDIFAM_NO_BANK, 80, 0, 127, MIDIFAM_PULSE_EXPRESSION, 1,
    midifam_gm_setup, midifam_effects_off_cc, midifam_gm_bank, NULL, NULL,
    midifam_no_drum_setup, NULL
};

/* ---- GS ---- */

uint8_t midifam_gs_reset[]  = { 0xF0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7F, 0x00, 0x41, 0xF7 };
/* GS master volume, the SC-55 predates GM's */
uint8_t midifam_gs_volume[] = { 0xF0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x04, 0x7F, 0x3D, 0xF7 };

void midifam_gs_setup() {
    midifam_sysex(midifam_gs_reset, sizeof(midifam_gs_reset), MIDIFAM_RESET_US);
    midifam_sysex(midifam_gs_volume, sizeof(midifam_gs_volume), MIDIFAM_SYSEX_US);
}

/* MT-32 set: GS MT-32 map, bank 127, CM-64/32L drums, program 127
 * (SC-55 manual p. 69, 21, 31) */
int midifam_gs_bank(int set) {
    return set == MIDISET_MT32 ? 127 : 0;
}

void midifam_gs_drum_setup(int set) {
    midi_program(MIDIFAM_DRUMS, set == MIDISET_MT32 ? 127 : 0);
}

/* Bank 8 program 80, the GS Sine Wave: one oscillator. The Square Wave
 * is two, about 5 Hz apart at A5, and their beating broke the pulses.
 * Pulses end with sound off (120): measured on an SC-55, expression 0
 * ended them up to 21 ms late, one train in four lost */
midi_family_t midifam_gs = {
    "GS", "GS", MIDISET_GM, 8, 80, 0, 127, MIDIFAM_PULSE_SOUND_OFF, 1,
    midifam_gs_setup, midifam_effects_off_cc, midifam_gs_bank, NULL, NULL,
    midifam_gs_drum_setup, NULL
};

/* ---- XG, as GM with its own reset, no Yamaha measured yet ---- */

uint8_t midifam_xg_on[] = { 0xF0, 0x43, 0x10, 0x4C, 0x00, 0x00, 0x7E, 0x00, 0xF7 };

void midifam_xg_setup() {
    midifam_sysex(midifam_xg_on, sizeof(midifam_xg_on), MIDIFAM_RESET_US);
    midifam_sysex(midifam_gm_volume, sizeof(midifam_gm_volume), MIDIFAM_SYSEX_US);
}

/* And the variation effect */
void midifam_xg_effects(uint8_t channel) {
    midifam_effects_off_cc(channel);
    midi_control(channel, MIDI_CC_VARIATION, 0);
}

int midifam_xg_bank(int set) {
    return set == MIDISET_GM ? 0 : MIDIFAM_CANT;
}

midi_family_t midifam_xg = {
    "XG", "XG", MIDISET_GM, MIDIFAM_NO_BANK, 80, 0, 127, MIDIFAM_PULSE_EXPRESSION, 1,
    midifam_xg_setup, midifam_xg_effects, midifam_xg_bank, NULL, NULL,
    midifam_no_drum_setup, NULL
};

/* ---- MT-32, CM-32L, LAPC-I ---- */

/* MT-32 MIDI Implementation, 1987. Reverb level 0 is the lowest, not
 * off. Master volume 100 */
uint8_t midifam_mt32_reverb_level[] = { 0xF0, 0x41, 0x10, 0x16, 0x12, 0x10, 0x00, 0x03, 0x00, 0x6D, 0xF7 };
uint8_t midifam_mt32_volume[] = { 0xF0, 0x41, 0x10, 0x16, 0x12, 0x10, 0x00, 0x16, 0x64, 0x76, 0xF7 };

/* All parameter reset, first. Without it back to back takes played
 * Organ and Brass in one of two ways, with it the same (measured) */
uint8_t midifam_mt32_reset[]  = { 0xF0, 0x41, 0x10, 0x16, 0x12, 0x7F, 0x00, 0x00, 0x00, 0x01, 0xF7 };

/* Part 1 reverb switch off, a program change loads it again. With
 * reverb on, a copy of each note came out the other side at -32 dB */
uint8_t midifam_mt32_reverb_off[] = { 0xF0, 0x41, 0x10, 0x16, 0x12, 0x03, 0x00, 0x06, 0x00, 0x77, 0xF7 };

/* Custom tone, loaded like games do: Timbre Memory 1 and Patch Memory 48
 * (program 47) at setup, nothing for it during the sequence. The ROM
 * tone is a few oscillators cents apart and beats 14 dB (measured),
 * the custom one is one LA square partial, filter open, level held,
 * release 0 so the note off ends the pulses. Neutral values from munt.
 * Measured: one line at 883.83 Hz (A5), held within 0.1 dB, ToneLevel
 * 8.3 dB down at velocity 71 */
uint8_t midifam_mt32_tone_timbre[] = {
    0xF0, 0x41, 0x10, 0x16, 0x12, 0x08, 0x00, 0x00,
    /* Common: name, both structures synth, partial 1 only, sustain */
    'M', 'D', 'F', ' ', 'T', 'o', 'n', 'e', ' ', ' ', 0, 0, 0x01, 0,
    /* WG: coarse, fine, keyfollow 1, no bender, square, PCM, 50%, PW sens */
    36, 50, 11, 0, 0, 0, 0, 7,
    /* Pitch envelope: none */
    0, 0, 0, 0, 0, 0, 0, 50, 50, 50, 50, 50,
    /* Pitch LFO: none */
    0, 0, 0,
    /* TVF: open, keyfollow as the pitch so the cutoff stays, no envelope */
    100, 0, 11, 64, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 100, 100, 100,
    /* TVA: level, velocity sensitivity, no bias, attack and release 0,
     * levels held */
    100, 75, 64, 12, 64, 12, 0, 0, 0, 0, 0, 0, 0, 100, 100, 100, 100,
    0, 0xF7
};

/* Patch Memory: timbre memory 1, no shift, reverb off */
uint8_t midifam_mt32_tone_patch[] = {
    0xF0, 0x41, 0x10, 0x16, 0x12, 0x05, 0x02, 0x78,
    2, 0, 24, 50, 12, 0, 0, 0,
    0, 0xF7
};

/* DT1 checksum, address and data */
void midifam_roland_checksum(uint8_t *message, unsigned length) {
    unsigned i, sum = 0;

    for (i = 5; i < length - 2; i++)
        sum += message[i];
    message[length - 2] = (uint8_t)((128 - (sum & 0x7F)) & 0x7F);
}

/* Not measured, generous */
#define MT32_MEMORY_WAIT_US  MIDIFAM_RESET_US

/* LCD, 20 characters at 20 00 00 */
uint8_t midifam_mt32_display[] = {
    0xF0, 0x41, 0x10, 0x16, 0x12, 0x20, 0x00, 0x00,
    ' ', 'M', 'D', 'F', 'o', 'u', 'r', 'i', 'e', 'r', ' ',
    'D', 'O', 'S', ' ', '0', '.', '9', '6', ' ',
    0, 0xF7
};

void midifam_mt32_setup() {
    printf(" MIDI: MT-32 reset, LCD text and custom tone to its memory\n");
    midifam_sysex(midifam_mt32_reset, sizeof(midifam_mt32_reset), MIDIFAM_RESET_US);
    midifam_roland_checksum(midifam_mt32_display, sizeof(midifam_mt32_display));
    midifam_sysex(midifam_mt32_display, sizeof(midifam_mt32_display), MIDIFAM_SYSEX_US);
    midifam_sysex(midifam_mt32_reverb_level, sizeof(midifam_mt32_reverb_level), MIDIFAM_SYSEX_US);
    midifam_sysex(midifam_mt32_volume, sizeof(midifam_mt32_volume), MIDIFAM_SYSEX_US);
    midifam_roland_checksum(midifam_mt32_tone_timbre, sizeof(midifam_mt32_tone_timbre));
    midifam_sysex(midifam_mt32_tone_timbre, sizeof(midifam_mt32_tone_timbre), MT32_MEMORY_WAIT_US);
    midifam_roland_checksum(midifam_mt32_tone_patch, sizeof(midifam_mt32_tone_patch));
    midifam_sysex(midifam_mt32_tone_patch, sizeof(midifam_mt32_tone_patch), MT32_MEMORY_WAIT_US);
}

/* Set by sysex after each program change */
void midifam_mt32_effects(uint8_t channel) {
}

int midifam_mt32_bank(int set) {
    return set == MIDISET_MT32 ? MIDIFAM_NO_BANK : MIDIFAM_CANT;
}

/* Programs load their own reverb switch, off after each. The tone's
 * patch has it off */
unsigned midifam_mt32_fixups(int tone) {
    return tone ? 0 : 1;
}

void midifam_mt32_fixup(int tone, unsigned index) {
    midi_sysex(midifam_mt32_reverb_off, sizeof(midifam_mt32_reverb_off));
}

/* Rhythm keys have their own reverb switch, off for the keys we play */
uint8_t midifam_mt32_key_reverb[11] = { 0xF0, 0x41, 0x10, 0x16, 0x12, 0x03, 0, 0, 0, 0, 0xF7 };

void midifam_mt32_drum_setup(int set) {
    unsigned drum, address;
    uint8_t  *message = midifam_mt32_key_reverb;

    for (drum = 0; drum < MIDISET_DRUMS; drum++) {
        address = (0x01 << 7) + 0x10 + (midi_sets[set].drums[drum] - 24) * 4 + 3;
        message[6] = (uint8_t)(address >> 7);
        message[7] = (uint8_t)(address & 0x7F);
        message[8] = 0;
        midifam_roland_checksum(message, sizeof(midifam_mt32_key_reverb));
        midifam_sysex(message, sizeof(midifam_mt32_key_reverb), MIDIFAM_SYSEX_US);
    }
}

/* The games' timbres into Timbre Memory 2 on, one message each as the
 * games sent them, and their programs' patches pointed at them, reverb
 * off. Patch values as the games had them. The clock timbre after them */
uint8_t midifam_mt32_memory[10 + MIDICUST_TIMBRE_SIZE] = {
    0xF0, 0x41, 0x10, 0x16, 0x12
};

void midifam_mt32_load(midi_custom_t *custom, unsigned slot) {
    uint8_t  *message = midifam_mt32_memory;
    unsigned byte, address;

    message[5] = 0x08;
    message[6] = (uint8_t)(slot * 2);
    message[7] = 0x00;
    for (byte = 0; byte < MIDICUST_TIMBRE_SIZE; byte++)
        message[8 + byte] = custom->timbre[byte];
    /* P-LFO DEPTH, 15h in each of the 4 partials after the 14 common bytes */
    if (custom->lfo_off)
        for (byte = 0; byte < 4; byte++)
            message[8 + 14 + byte * 58 + 0x15] = 0;
    message[8 + MIDICUST_TIMBRE_SIZE + 1] = 0xF7;
    midifam_roland_checksum(message, 10 + MIDICUST_TIMBRE_SIZE);
    midifam_sysex(message, 10 + MIDICUST_TIMBRE_SIZE, MT32_MEMORY_WAIT_US);

    address = custom->instrument.program * 8;
    message[5]  = 0x05;
    message[6]  = (uint8_t)(address >> 7);
    message[7]  = (uint8_t)(address & 0x7F);
    message[8]  = 2;                        /* timbre memory */
    message[9]  = (uint8_t)slot;
    message[10] = 24;                       /* key shift none */
    message[11] = 50;                       /* fine tune none */
    message[12] = 12;                       /* bender range */
    message[13] = 0;                        /* assign mode */
    message[14] = 0;                        /* reverb off */
    message[15] = 0;
    message[17] = 0xF7;
    midifam_roland_checksum(message, 18);
    midifam_sysex(message, 18, MT32_MEMORY_WAIT_US);
}

void midifam_mt32_custom_load(midi_custom_t *customs, int count, midi_custom_t *clock) {
    unsigned custom;

    printf(" MIDI: %d game timbres and the clock timbre to its memory\n", count);
    for (custom = 0; custom < (unsigned)count; custom++)
        midifam_mt32_load(&customs[custom], custom + 1);
    if (clock)
        midifam_mt32_load(clock, count + 1);
}

/* Program 47, the custom tone. Pans the other way round, 0 is right
 * (measured, SC-55 manual p. 32). No sound off (120) */
midi_family_t midifam_mt32 = {
    "MT-32", "MT32", MIDISET_MT32, MIDIFAM_NO_BANK, 47, 127, 0, MIDIFAM_PULSE_NOTE_OFF, 0,
    midifam_mt32_setup, midifam_mt32_effects, midifam_mt32_bank,
    midifam_mt32_fixups, midifam_mt32_fixup, midifam_mt32_drum_setup,
    midifam_mt32_custom_load
};

midi_family_t *midi_families[MIDIFAM_COUNT] = {
    &midifam_gm, &midifam_gs, &midifam_xg, &midifam_mt32
};

midi_family_t *midifam_find(char *option) {
    int family;

    for (family = 0; family < MIDIFAM_COUNT; family++)
        if (!stricmp(option, midi_families[family]->option))
            return midi_families[family];
    return NULL;
}
