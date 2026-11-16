#include <string.h>
#include "midiset.h"
#include "profile.h"

/* Roles and note ranges from the MT-32 music of Loom, Monkey Island,
 * Silpheed, Castlevania and Dune. Crash and open hi-hat go first, as
 * fifth the crash still rang at -70 dBr in the closing Silence. GS
 * synths play the MT-32 set from their MT-32 map (bank 127) */
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
      { 49, 46, 36, 38, 42, 37, 63, 75 }, 0, NULL },
    { "MT-32", "MT32", PROFILE_MT32_NAME, PROFILE_MT32_FILE, {
        { "Strings",  48, 36, 96 },     /* Str Sect 1 */
        { "Organ",     9, 36, 96 },     /* Elec Org 2 */
        { "Bass",     66, 28, 55 },     /* Elec Bass 1 */
        { "Brass",    88, 55, 88 },     /* Trumpet 1 */
        { "Woodwind", 73, 60, 96 },     /* Flute 2 */
        { "Mallet",  104, 48, 84 },     /* Marimba */
        { "Plucked",  58, 31, 91 },     /* Harp 2 */
        { "Synth",    24, 36, 96 } },   /* Syn Brass 1 */
      { 49, 46, 36, 38, 42, 37, 63, 75 }, MIDISET_CUSTOMS, midi_customs }
};

int midiset_find(char *option) {
    int set;

    for (set = 0; set < MIDISET_COUNT; set++)
        if (!stricmp(option, midi_sets[set].option))
            return set;
    return -1;
}

uint8_t midiset_note(midi_instrument_t *instrument, unsigned step) {
    unsigned span = instrument->high - instrument->low;

    return (uint8_t)(instrument->low + (span * step + (MIDISET_NOTES - 1) / 2) / (MIDISET_NOTES - 1));
}
