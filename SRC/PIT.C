#include <conio.h>
#include "pit.h"

/*
 * https://expiredpopsicle.com/articles/2017-04-13-DOS_Timer_Stuff/2017-04-13-DOS_Timer_Stuff.html
 * https://groups.google.com/g/comp.os.msdos.programmer/c/wE4xEMcvPvY?pli=1
 */

/*
 * PIT clock is 105/88 MHz (14.31818 MHz / 12)
 * us to PIT clocks is us * 105 / 88.
 * Overflows past 40.9s, beyond what we need
 */
#define PIT_MUL 105UL
#define PIT_DIV 88UL

#define PIT_CMD 0x43
#define PIT_CH0 0x40

uint16_t pit_read() {
    uint16_t lo, hi;

    /* latch command, counter 0 */
    outp(PIT_CMD, 0x00);
    lo = (uint16_t)inp(PIT_CH0);
    hi = (uint16_t)inp(PIT_CH0);
    return (uint16_t)((hi << 8) | lo);
}

void pit_delay_us(uint32_t us) {
    uint32_t clocks_needed;
    uint16_t prev;

    /* rounded up, minimum delay */
    clocks_needed = (us * PIT_MUL + (PIT_DIV - 1UL)) / PIT_DIV;

    prev = pit_read();

    while (clocks_needed != 0) {
        uint16_t cur, counter_delta, clocks_elapsed;

        cur = pit_read();

        /*
         * Unsigned 16-bit subtraction handles the
         * counter wrapping from 0 back to 65535.
         */
        counter_delta = (uint16_t)(prev - cur);

        /*
         * In PIT Mode 3, the counter changes twice
         * for each input clock, so convert counter
         * units to PIT clocks.
         */
        clocks_elapsed = (uint16_t)(counter_delta / 2u);

        if ((uint32_t)clocks_elapsed > clocks_needed)
            clocks_needed = 0;
        else
            clocks_needed -= clocks_elapsed;
        prev = cur;
    }
}
