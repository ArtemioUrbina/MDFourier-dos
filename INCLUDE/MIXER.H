#ifndef MIXER_H
#define MIXER_H

#include "env.h"

/* Print the mixer state that affects a capture, with warnings.
 * Mixer type from the BLASTER T value; SB2.0 and older have none */
int mixer_report(blaster_cfg_t *cfg);

/* Option /M sets FM path at max, no boost, tone flat, other inputs muted.
 * Saves the previous state for mixer_restore() */
int mixer_set_standard(blaster_cfg_t *cfg);
void mixer_restore();

#endif
