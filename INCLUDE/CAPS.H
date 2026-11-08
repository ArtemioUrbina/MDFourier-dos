#ifndef CAPS_H
#define CAPS_H

#include "dsp.h"

/*
 * What this machine has to play the sequence with. MAIN.C fills it in
 * once, before the sequence starts, and hands it to the sequence, so
 * each one picks its sources from a single place instead of a set of
 * separate flags.
 */

/* FM chip at 0x388 */
#define CAPS_FM_NONE    0
#define CAPS_FM_OPL2    1
#define CAPS_FM_OPL3    2

typedef struct {
    int           fm;               /* CAPS_FM_NONE, CAPS_FM_OPL2 or CAPS_FM_OPL3 */

    int           cms;              /* CMS_NONE, CMS_LATCH or CMS_ON_SB, from cms_detect() */
    unsigned      cms_port;

    int           dsp;              /* 1 when the DSP answered its reset and version */
    dsp_version_t dsp_version;
    int           dsp_ess;          /* an ESS AudioDrive behind a DSP 3.xx */
    int           sbpro_compatible; /* DSP 3.xx, not ESS: a WSS codec may sit next to it */
    int           pcm;              /* 8-bit mono PCM plays, pcm_init() passed */
    int           adpcm;            /* the DSP takes ADPCM, pcm_adpcm_probe() */

    int           mixer;            /* MIXER_UNKNOWN, MIXER_WARN, MIXER_OK or MIXER_NONE */
    int           mixer_ok;         /* the mixer was verified, recorded in the watermark tone */
} caps_t;

#endif
