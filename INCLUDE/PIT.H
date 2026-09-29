#ifndef PIT_H
#define PIT_H

#include <stdint.h>

/* PIT input clock: 105/88 MHz */
#define PIT_HZ_F (105000000.0 / 88.0)
/* us to PIT clocks, rounded up. Valid up to ~40 s */
#define PIT_US_TO_CLK(us) (((uint32_t)(us) * 105UL + 87UL) / 88UL)

void pit_init(); 

/* Running time in PIT clocks. Must be called every ~27 ms */
uint32_t pit_now(); 

void pit_delay_us(uint32_t us);

#endif
