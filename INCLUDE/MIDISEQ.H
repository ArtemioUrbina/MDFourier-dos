#ifndef MIDISEQ_H
#define MIDISEQ_H

/* MIDI sequence (/M), a prototype with its own profile:
 * DOS: MIDI prototype. Needs midi_init() */

/* Resets the synth and sets its channel up, before the timing starts.
 * mt32: the MT-32's program numbers (/MT) */
void midiseq_setup(int mt32);

/* SEQ_OK, SEQ_ABORTED or SEQ_BAD_FRAMES */
int  midiseq_run();

#endif
