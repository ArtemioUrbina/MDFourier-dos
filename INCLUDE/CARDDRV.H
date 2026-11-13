#ifndef CARDDRV_H
#define CARDDRV_H

#include <stdint.h>
#include "env.h"

/*
 * Cards next to an SB Pro compatible DSP that need more than the SB
 * mixer: a WSS codec for 16-bit, their own mixer or effects, levels to
 * set right before playing. Each one is a card_t of hooks, NULL where it
 * has nothing to do, and CARDS.C calls them in the order of its list.
 *
 * The order matters on the Crystal: any access to its codec switches the
 * card to WSS mode and any SB access switches it back, and every DSP
 * command rewrites the codec levels from the SB ones. MAIN.C calls the
 * cards_ functions at the points in its setup where each step belongs.
 */
typedef struct {
    char *name;
    /* Main sequence: find the card and pick its 16-bit codec */
    void (*detect)(blaster_cfg_t *cfg);
    /* The card has a codec MDF can't play without /W: stop */
    int  (*needs_config)();
    /* Before the SB mixer report: what its SB mixer bits really do */
    void (*mixer_notes)();
    /* Its own mixer for capture: -1 none, 0 not accepted, 1 set */
    int  (*mixer_set)();
    /* Effects that alter the output, off for capture, back at exit */
    void (*effects_off)();
    /* Its own mixer: MIXER_NONE when it has none */
    int  (*mixer_report)();
    /* An effect still on, kept by /K, and what to say about it */
    int  (*effects_on)();
    char *effects_warning;
    /* /V and /R: what the card's own registers hold */
    void (*report)(uint16_t sb_base);
    /* Right before playing, after the setup's last DSP command */
    void (*before_play)(uint16_t sb_base);
} card_t;

extern card_t sax_card;
extern card_t crystal_card;

void cards_detect(blaster_cfg_t *cfg);
int  cards_need_config();
void cards_mixer_notes();
/* Combined with the SB mixer's result, as mixer_set_standard() gives it */
int  cards_mixer_set(int sb_result);
void cards_effects_off();
/* The SB mixer's result, made worse by any card's */
int  cards_mixer_report(int sb_mixer);
/* The same for an effect still on, with its warning */
int  cards_effects_check(int mixer);
void cards_report(uint16_t sb_base);
void cards_before_play(uint16_t sb_base);

#endif
