#ifndef OPL_H
#define OPL_H

#include <stdint.h>
/* AdLib/OPL2/OPL3 FM driver */

/* 1 if the timer test finds a chip at opl_port */
int  opl_detect();
/* Every register to a known value, notes off */
void opl_init();
void opl_reset();

/* Adlib FM I/O port not in BLASTER */
extern uint16_t opl_port;

void opl_write(uint8_t reg, uint8_t val);
int opl_is_opl3();

/* stereo panning (0xC0 bits 4-5) */
void opl3_set_new_mode(int on);

#endif
