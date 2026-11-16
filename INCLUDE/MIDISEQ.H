#ifndef MIDISEQ_H
#define MIDISEQ_H

#include "midifam.h"

/* MIDI sequence (/M), needs midi_init() */

/* Resets the synth and sets the channel up, before the timing */
void midiseq_setup(midi_family_t *family, int set);

/* SEQ_OK, SEQ_ABORTED or SEQ_BAD_FRAMES */
int  midiseq_run();

#endif
