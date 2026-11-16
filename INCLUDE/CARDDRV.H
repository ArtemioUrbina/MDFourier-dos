#ifndef CARDDRV_H
#define CARDDRV_H

#include <stdint.h>
#include "env.h"

/* Cards next to an SB Pro DSP that need more than the SB mixer: a WSS
 * codec, their own mixer or effects. Hooks, NULL where nothing to do.
 * Order matters on the Crystal: codec access switches it to WSS, SB
 * access back, and every DSP command rewrites the codec levels */
typedef struct {
    char *name;
    /* Main sequence: find the card and pick its 16-bit codec */
    void (*detect)(blaster_cfg_t *cfg);
    /* A codec MDF can't play without /W: stop */
    int  (*needs_config)();
    /* Before the SB mixer report: what its SB mixer bits really do */
    void (*mixer_notes)();
    /* Own mixer: -1 none, 0 not accepted, 1 set */
    int  (*mixer_set)();
    /* Effects that alter the output, off for capture, back at exit */
    void (*effects_off)();
    /* MIXER_NONE when none */
    int  (*mixer_report)();
    /* An effect kept by /K, and what to say */
    int  (*effects_on)();
    char *effects_warning;
    /* /V and /R: the card's own registers */
    void (*report)(uint16_t sb_base);
    /* Right before playing, after the setup's last DSP command */
    void (*before_play)(uint16_t sb_base);
} card_t;

extern card_t sax_card;
extern card_t crystal_card;

void cards_detect(blaster_cfg_t *cfg);
int  cards_need_config();
void cards_mixer_notes();
/* With the SB mixer's result */
int  cards_mixer_set(int sb_result);
void cards_effects_off();
/* The SB mixer's result, worse by any card's */
int  cards_mixer_report(int sb_mixer);
/* The same for an effect still on, with its warning */
int  cards_effects_check(int mixer);
void cards_report(uint16_t sb_base);
void cards_before_play(uint16_t sb_base);

#endif
