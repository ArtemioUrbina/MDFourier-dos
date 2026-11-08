#ifndef PCMBLK_H
#define PCMBLK_H

#include "pcm.h"

/* 8-bit mono PCM and ADPCM blocks for the sequences. */

void pcmblk_clock(unsigned frames);
void pcmblk_steps(pcm_steps_t kind, unsigned steps, unsigned frames);

void pcmblk_adpcm_clock(unsigned frames);
void pcmblk_adpcm_steps(pcm_adpcm_t format, pcm_steps_t kind, unsigned steps, unsigned frames);

#endif
