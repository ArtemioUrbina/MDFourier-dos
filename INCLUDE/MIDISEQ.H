#ifndef MIDISEQ_H
#define MIDISEQ_H

#include "midifam.h"

/* MIDI sequence (/M). Needs midi_init() */

/* Resets the synth the family's way and sets its channel up, before
 * the timing starts. set: MIDISET_GM or MIDISET_MT32 */
void midiseq_setup(midi_family_t *family, int set);

/* SEQ_OK, SEQ_ABORTED or SEQ_BAD_FRAMES */
int  midiseq_run();

#endif
