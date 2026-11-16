#ifndef MIDIFAM_H
#define MIDIFAM_H

#include <stdint.h>

/*
 * MIDI families: how we talk to a synth. Its reset, master volume and
 * effects, the clean tone the sync and the tuning play, what a program
 * change needs after it, how a pulse ends and the last note stops, and
 * how it plays a content set's programs and drums (MIDISET.H). Every
 * message a family sends is one measured to be needed on it, or says
 * where it comes from.
 */

#define MIDIFAM_NO_BANK     -2      /* no bank select sent */
#define MIDIFAM_CANT        -1      /* the family can't play that set */

/* How a sync pulse ends, after its two frames */
#define MIDIFAM_PULSE_NOTE_OFF      0   /* the note off alone: the tone's release is 0 */
#define MIDIFAM_PULSE_EXPRESSION    1   /* the expression to 0 as well */
#define MIDIFAM_PULSE_SOUND_OFF     2   /* sound off (120) as well */

typedef struct {
    char     *name;                 /* as printed */
    char     *option;               /* as given after /M: */
    int      set;                   /* the content set it plays by default */
    int      tone_bank;             /* bank select before the tone, or MIDIFAM_NO_BANK */
    uint8_t  tone_program;
    uint8_t  pan_left, pan_right;
    int      pulse_end;             /* MIDIFAM_PULSE_... */
    int      sound_off;             /* takes controller 120, sound off */
    void     (*setup)();            /* reset, master volume */
    void     (*effects_off)(uint8_t channel);
    int      (*set_bank)(int set);  /* bank for a set's programs, MIDIFAM_NO_BANK or MIDIFAM_CANT */
    unsigned (*fixups)(int tone);   /* messages after a program change, NULL for none */
    void     (*fixup)(int tone, unsigned index);
    void     (*drum_setup)(int set);    /* the drum kit on channel 10, before the sequence */
} midi_family_t;

#define MIDIFAM_GM      0
#define MIDIFAM_GS      1
#define MIDIFAM_XG      2
#define MIDIFAM_MT32    3
#define MIDIFAM_COUNT   4

extern midi_family_t *midi_families[MIDIFAM_COUNT];

/* The family named by an /M: option, or NULL */
midi_family_t *midifam_find(char *option);

/* Channel 10, the drums on every family */
#define MIDIFAM_DRUMS       9

/* Sends a sysex message and waits, before the sequence only */
void midifam_sysex(uint8_t *data, unsigned length, uint32_t wait_us);

/* The resets take a while. The MT-32's early firmware also loses
 * messages that come too close together */
#define MIDIFAM_RESET_US    300000UL
#define MIDIFAM_SYSEX_US    50000UL

#endif
