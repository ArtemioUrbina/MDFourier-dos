#ifndef DSP_H
#define DSP_H

#include <stdint.h>

typedef struct {
    uint8_t major;
    uint8_t minor;
} dsp_version_t;

int dsp_reset(uint16_t base);
int dsp_get_version(uint16_t base, dsp_version_t *ver);

#endif
