#ifndef MDF_H
#define MDF_H

#include <stdint.h>
#include "opltone.h"

#define MDF_OK          0   /* complete, every frame on time */
#define MDF_ABORTED     1   /* ESC pressed */
#define MDF_BAD_FRAMES  2   /* complete, but frames out of tolerance */

int mdf_run_full_test(uint8_t channel, unsigned frames, int use_stereo, int mixer_ok);

#endif
