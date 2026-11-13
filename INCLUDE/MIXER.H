#ifndef MIXER_H
#define MIXER_H

#include "env.h"

/* mixer_report() results */
#define MIXER_UNKNOWN  -1   /* no BLASTER, not answering, unknown type */
#define MIXER_WARN      0   /* problems, on a mixer the program can set */
#define MIXER_OK        1   /* OK for capture */
#define MIXER_NONE      2   /* SB 1.x/2.0: no mixer chip, only the knob */

/* Print the mixer state that affects a capture, with warnings.
 * Mixer type from the BLASTER T value; SB2.0 and older have none */
int mixer_report(blaster_cfg_t *cfg);

/* Sets FM path at max, no boost, tone flat, other inputs muted.
 * Saves the previous state for mixer_restore() */
int mixer_set_standard(blaster_cfg_t *cfg);
void mixer_restore();

/* The SB Pro output filter bit is kept but changes nothing on this
 * card (CS4237B): set and shown, not warned about. Before the report */
void mixer_sbpro_filter_unused();

/* SB Pro output filter on or off, for the unfiltered sweeps. Does
 * nothing unless the mixer setup set 0Eh */
void mixer_sbpro_filter(int on);
int  mixer_sbpro_filter_switched();

/* SB Pro mixer 0Eh for the 8-bit stereo section, and back as it was.
 * MIXER.C is the only one that writes 0Eh */
void mixer_sbpro_stereo_on(uint16_t base);
void mixer_sbpro_stereo_off();

/* Mixer register access, used by pcm.c */
uint8_t mix_read(uint16_t base, uint8_t reg);
/* ESS ES18xx, by the ID at mixer 0x40 */
int     mixer_is_ess(uint16_t base);
void    mix_write(uint16_t base, uint8_t reg, uint8_t val);

#endif
