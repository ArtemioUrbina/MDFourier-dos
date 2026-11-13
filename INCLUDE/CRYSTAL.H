#ifndef CRYSTAL_H
#define CRYSTAL_H

#include <stdint.h>

/* Crystal CS4236 family codec next to an SB Pro compatible DSP */

/* Where the codec's registers start, 534h on the Orpheus II, 0 if none.
 * Looked for once, "report" prints what sits at the ports that isn't one */
unsigned crystal_find(int report);

/* The codec found is a CS4237B, revision C or later: X25 names it.
 * Revision B reads X25 as 00h like the CS4236, and isn't counted */
int crystal_is_cs4237b();

/* For /V: finds the codec, prints its levels and the card's SB/WSS
 * context. Its WSSbase, 0 if none */
unsigned crystal_check(uint16_t sb_base);

/* With the mixer setup: SRS 3D off, back at exit */
void crystal_srs_off();
/* SRS 3D still on: left by /K */
int  crystal_srs_on();

/* Right before playing: the wavetable port (an external FM chip) to
 * 0 dB, back at exit */
void crystal_synth_unity(uint16_t sb_base);

#endif
