#ifndef OPL_H
#define OPL_H

#include <stdint.h>
#include "carddrv.h"

/* AdLib/OPL2/OPL3 FM driver */
extern fm_driver_t opl_driver;

/* Adlib FM I/O port not in BLASTER */
extern const uint16_t opl_port;

void opl_write(uint8_t reg, uint8_t val);
int opl_is_opl3();

#endif
