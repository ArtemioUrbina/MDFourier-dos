#ifndef SB2SEQ_H
#define SB2SEQ_H

#include "caps.h"

/* CMS sequence (/C): CMS, FM, 8-bit PCM and ADPCM, synced by FM or by CMS.
 * SEQ_OK, SEQ_ABORTED or SEQ_BAD_FRAMES */
int sb2seq_run(caps_t *caps);

#endif
