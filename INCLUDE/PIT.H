#ifndef PIT_H
#define PIT_H

#include <stdint.h>

/* PIT input clock: 105/88 MHz */
#define PIT_HZ_F (105000000.0 / 88.0)
/* us to PIT clocks, rounded up. Valid up to ~40 s */
#define PIT_US_TO_CLK(us) (((uint32_t)(us) * 105UL + 87UL) / 88UL)

void pit_init(); 

/* Under Windows the PIT doesn't keep time, we use the TSC calibrated to
 * the BIOS ticks. If the ticks don't move the PIT stays */
int  pit_use_tsc();
/* CPU clock measured by pit_use_tsc() */
double pit_tsc_hz();
char  *pit_tsc_failure();

/* No TSC (486): PIT rate checked against the BIOS ticks */
int    pit_check_rate();
/* PIT clocks per BIOS tick it measured, 65536 when real */
double pit_ticks_measured();
char  *pit_rate_mode();

/* Running time in PIT clocks. Must be called every ~27 ms */
uint32_t pit_now(); 

void pit_delay_us(uint32_t us);

#endif
