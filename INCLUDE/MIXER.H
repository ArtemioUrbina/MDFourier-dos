#ifndef MIXER_H
#define MIXER_H

#include "env.h"

/* Print the mixer state that affects a capture, with warnings.
 * Mixer type from the BLASTER T value; SB2.0 and older have none */
void mixer_report(blaster_cfg_t *cfg);

#endif
