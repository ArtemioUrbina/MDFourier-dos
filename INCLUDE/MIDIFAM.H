#ifndef MIDIFAM_H
#define MIDIFAM_H

#include <stdint.h>
#include "midiset.h"

/* MIDI families: how we talk to each synth, reset, effects, the tone,
 * what goes after a program change and how a pulse ends */

#define MIDIFAM_NO_BANK     -2      /* no bank select sent */
#define MIDIFAM_CANT        -1      /* the family can't play that set */

/* How a sync pulse ends */
#define MIDIFAM_PULSE_NOTE_OFF      0   /* note off, the tone's release is 0 */
#define MIDIFAM_PULSE_EXPRESSION    1   /* and expression to 0 */
#define MIDIFAM_PULSE_SOUND_OFF     2   /* and sound off (120) */

typedef struct {
    char     *name;                 /* as printed */
    char     *option;
    int      set;                   /* default set */
    int      tone_bank;             /* or MIDIFAM_NO_BANK */
    uint8_t  tone_program;
    uint8_t  pan_left, pan_right;
    int      pulse_end;             /* MIDIFAM_PULSE_... */
    int      sound_off;             /* knows sound off (120) */
    void     (*setup)();            /* reset, master volume */
    void     (*effects_off)(uint8_t channel);
    int      (*set_bank)(int set);  /* MIDIFAM_NO_BANK or MIDIFAM_CANT */
    unsigned (*fixups)(int tone);   /* after a program change, NULL for none */
    void     (*fixup)(int tone, unsigned index);
    void     (*drum_setup)(int set);    /* drum kit setup */
    void     (*custom_load)(midi_custom_t *customs, int count, midi_custom_t *clock);  /* NULL: presets */
} midi_family_t;

#define MIDIFAM_GM      0
#define MIDIFAM_GS      1
#define MIDIFAM_XG      2
#define MIDIFAM_MT32    3
#define MIDIFAM_COUNT   4

extern midi_family_t *midi_families[MIDIFAM_COUNT];

/* The family named by an /M: option, or NULL */
midi_family_t *midifam_find(char *option);

/* Channel 10 */
#define MIDIFAM_DRUMS       9

/* Sysex and wait, before the sequence only */
void midifam_sysex(uint8_t *data, unsigned length, uint32_t wait_us);

/* Resets take a while, and the MT-32's early firmware loses messages
 * that come too close */
#define MIDIFAM_RESET_US    300000UL
#define MIDIFAM_SYSEX_US    50000UL

#endif
