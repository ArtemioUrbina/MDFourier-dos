#ifndef MIDISET_H
#define MIDISET_H

#include <stdint.h>

/*
 * MIDI content sets: what the typical blocks play, each set with its
 * own profile. Any two captures of the same set compare, whatever
 * family played it: an SC-55 through its MT-32 map against an MT-32.
 * Both sets play the same roles in the same order, so their plots read
 * the same way.
 */

#define MIDISET_GM          0
#define MIDISET_MT32        1
#define MIDISET_COUNT       2

#define MIDISET_INSTRUMENTS 8
#define MIDISET_NOTES       8       /* each instrument, low to high */
#define MIDISET_DRUMS       8

typedef struct {
    char    *name;          /* as in the profile */
    uint8_t program;        /* 0-based */
    uint8_t low, high;      /* the notes it plays, MIDISET_NOTES of them */
} midi_instrument_t;

typedef struct {
    char              *name;      /* as printed */
    char              *option;    /* as given after /M:family, */
    char              *profile_name;  /* its captures' MDFourier profile, as listed */
    char              *profile;   /* and its file */
    midi_instrument_t instruments[MIDISET_INSTRUMENTS];
    uint8_t           drums[MIDISET_DRUMS];   /* notes on channel 10 */
} midi_set_t;

extern midi_set_t midi_sets[MIDISET_COUNT];

/* The set named by an option, or -1 */
int midiset_find(char *option);

/* Note step of an instrument, low to high in equal steps */
uint8_t midiset_note(midi_instrument_t *instrument, unsigned step);

#endif
