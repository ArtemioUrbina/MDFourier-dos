#include <string.h>
#include "midiset.h"
#include "profile.h"

/*
 * The roles come from the MT-32 soundtracks of Loom, The Secret of
 * Monkey Island, Silpheed, Castlevania (Akumajo Dracula) and Dune,
 * every note counted with their sysex replayed: strings, organ, bass,
 * brass, woodwind, mallet, plucked and synth are what the games that
 * use the presets lean on. The note ranges are the ones they play
 * these in. The drums are the keys they hit most: kick, snare, the two
 * hi-hats, crash, rim, and Monkey Island's conga and claves. The crash
 * and the open hi-hat go first: measured on an MT-32, the crash still
 * rang at -70 dBr in the closing Silence when it was fifth.
 *
 * The GM set has the GM instrument for each role. The MT-32 set's
 * program numbers are the MT-32's, which a GS synth also plays from
 * its MT-32 map (bank 127, SC-55 manual p. 69).
 */
midi_set_t midi_sets[MIDISET_COUNT] = {
    { "GM", "GM", PROFILE_GM_NAME, PROFILE_GM_FILE, {
        { "Strings",  48, 36, 96 },     /* String Ensemble 1 */
        { "Organ",    16, 36, 96 },     /* Drawbar Organ */
        { "Bass",     33, 28, 55 },     /* Electric Bass (finger) */
        { "Brass",    56, 55, 88 },     /* Trumpet */
        { "Woodwind", 73, 60, 96 },     /* Flute */
        { "Mallet",   12, 48, 84 },     /* Marimba */
        { "Plucked",  46, 31, 91 },     /* Orchestral Harp */
        { "Synth",    62, 36, 96 } },   /* Synth Brass 1 */
      { 49, 46, 36, 38, 42, 37, 63, 75 } },
    { "MT-32", "MT32", PROFILE_MT32_NAME, PROFILE_MT32_FILE, {
        { "Strings",  48, 36, 96 },     /* Str Sect 1 */
        { "Organ",     9, 36, 96 },     /* Elec Org 2 */
        { "Bass",     66, 28, 55 },     /* Elec Bass 1 */
        { "Brass",    88, 55, 88 },     /* Trumpet 1 */
        { "Woodwind", 73, 60, 96 },     /* Flute 2 */
        { "Mallet",  104, 48, 84 },     /* Marimba */
        { "Plucked",  58, 31, 91 },     /* Harp 2 */
        { "Synth",    24, 36, 96 } },   /* Syn Brass 1 */
      { 49, 46, 36, 38, 42, 37, 63, 75 } }
};

int midiset_find(char *option) {
    int i;

    for (i = 0; i < MIDISET_COUNT; i++)
        if (!stricmp(option, midi_sets[i].option))
            return i;
    return -1;
}

uint8_t midiset_note(midi_instrument_t *instrument, unsigned step) {
    unsigned span = instrument->high - instrument->low;

    return (uint8_t)(instrument->low + (span * step + (MIDISET_NOTES - 1) / 2) / (MIDISET_NOTES - 1));
}
