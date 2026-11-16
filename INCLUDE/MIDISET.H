#ifndef MIDISET_H
#define MIDISET_H

#include <stdint.h>

/* What the instrument blocks play. Each set has its own profile, any
 * two captures of a set compare, whatever synth played them */

#define MIDISET_GM          0
#define MIDISET_MT32        1
#define MIDISET_COUNT       2

#define MIDISET_INSTRUMENTS 8
#define MIDISET_NOTES       8       /* each instrument, low to high */
#define MIDISET_DRUMS       8
#define MIDISET_CUSTOMS     5       /* MT-32 set only */

#define MIDICUST_TIMBRE_SIZE    246 /* common and 4 partials */

typedef struct {
    char    *name;          /* as in the profile */
    uint8_t program;        /* 0-based */
    uint8_t low, high;      /* the notes it plays, MIDISET_NOTES of them */
} midi_instrument_t;

/* A game's custom timbre, MT-32 set */
typedef struct {
    midi_instrument_t instrument;
    uint8_t           *timbre;
    int               lfo_off;      /* pitch LFO depth to 0 when loaded */
} midi_custom_t;

typedef struct {
    char              *name;
    char              *option;
    char              *profile_name;  /* MDFourier profile, name and file */
    char              *profile;
    midi_instrument_t instruments[MIDISET_INSTRUMENTS];
    uint8_t           drums[MIDISET_DRUMS];   /* notes on channel 10 */
    int               custom_count;
    midi_custom_t     *customs;
} midi_set_t;

extern midi_set_t    midi_sets[MIDISET_COUNT];
extern midi_custom_t midi_customs[MIDISET_CUSTOMS];

/* The set named by an option, or -1 */
int midiset_find(char *option);

/* Note for a step, low to high */
uint8_t midiset_note(midi_instrument_t *instrument, unsigned step);

#endif
