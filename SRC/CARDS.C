/*
 * The cards of CARDDRV.H, called in the order of the list: the OPL3-SAx
 * first, then the Crystal, the order MAIN.C called them in before they
 * had a list.
 */

#include <stdio.h>
#include "carddrv.h"
#include "mixer.h"

card_t *cards[] = { &sax_card, &crystal_card };

#define CARD_COUNT  (sizeof(cards) / sizeof(cards[0]))

void cards_detect(blaster_cfg_t *cfg) {
    unsigned i;

    for (i = 0; i < CARD_COUNT; i++)
        if (cards[i]->detect)
            cards[i]->detect(cfg);
}

int cards_need_config() {
    unsigned i;

    for (i = 0; i < CARD_COUNT; i++)
        if (cards[i]->needs_config && cards[i]->needs_config())
            return 1;
    return 0;
}

void cards_mixer_notes() {
    unsigned i;

    for (i = 0; i < CARD_COUNT; i++)
        if (cards[i]->mixer_notes)
            cards[i]->mixer_notes();
}

/* Both, or whichever this card has */
int cards_mixer_set(int sb_result) {
    int      result = sb_result;
    unsigned i;

    for (i = 0; i < CARD_COUNT; i++) {
        int card_result;

        if (!cards[i]->mixer_set)
            continue;
        card_result = cards[i]->mixer_set();
        if (result == -1)
            result = card_result;
        else if (card_result != -1)
            result &= card_result;
    }
    return result;
}

void cards_effects_off() {
    unsigned i;

    for (i = 0; i < CARD_COUNT; i++)
        if (cards[i]->effects_off)
            cards[i]->effects_off();
}

/* A card's own mixer report counts when it has one and it isn't OK */
int cards_mixer_report(int sb_mixer) {
    int      mixer = sb_mixer;
    unsigned i;

    for (i = 0; i < CARD_COUNT; i++) {
        if (cards[i]->mixer_report) {
            int card_mixer = cards[i]->mixer_report();

            if (card_mixer != MIXER_NONE && card_mixer != MIXER_OK && mixer != MIXER_UNKNOWN)
                mixer = card_mixer;
        }
    }
    return mixer;
}

/* An effect left on by /K makes the mixer not verified */
int cards_effects_check(int mixer) {
    unsigned i;

    for (i = 0; i < CARD_COUNT; i++) {
        if (cards[i]->effects_on && cards[i]->effects_on()) {
            printf("  WARNING: %s\n", cards[i]->effects_warning);
            if (mixer != MIXER_UNKNOWN)
                mixer = MIXER_WARN;
        }
    }
    return mixer;
}

void cards_report(uint16_t sb_base) {
    unsigned i;

    for (i = 0; i < CARD_COUNT; i++)
        if (cards[i]->report)
            cards[i]->report(sb_base);
}

void cards_before_play(uint16_t sb_base) {
    unsigned i;

    for (i = 0; i < CARD_COUNT; i++)
        if (cards[i]->before_play)
            cards[i]->before_play(sb_base);
}
