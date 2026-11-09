#ifndef CRYSTAL_H
#define CRYSTAL_H

#include <stdint.h>

/* Crystal CS4236 family codec next to an SB Pro compatible DSP */

/* For /V: finds the codec, prints its levels and the card's SB/WSS
 * context. Its WSSbase, 0 if none */
unsigned crystal_check(uint16_t sb_base);

/* Right before playing: the wavetable port (an external FM chip) to
 * 0 dB, back at exit */
void crystal_synth_unity(uint16_t sb_base);

#endif
