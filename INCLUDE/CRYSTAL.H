#ifndef CRYSTAL_H
#define CRYSTAL_H

#include <stdint.h>

/* Crystal CS4236 family codec next to an SB Pro compatible DSP */

/* Codec base, 534h on the Orpheus II, 0 if none. Looked for once,
 * report says what's at the ports */
unsigned crystal_find(int report);

/* A CS4237B rev C or later, X25 names it. Rev B reads 00h */
int crystal_is_cs4237b();

/* /V: codec levels and SB/WSS context. Its base, 0 if none */
unsigned crystal_check(uint16_t sb_base);

/* With the mixer setup: SRS 3D off, back at exit */
void crystal_srs_off();
/* SRS 3D still on: left by /K */
int  crystal_srs_on();

/* Right before playing: wavetable port to 0 dB, back at exit */
void crystal_synth_unity(uint16_t sb_base);

#endif
