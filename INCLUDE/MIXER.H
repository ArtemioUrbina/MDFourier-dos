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

#endif
