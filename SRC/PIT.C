#include <conio.h>
#include "pit.h"

/*
 * https://expiredpopsicle.com/articles/2017-04-13-DOS_Timer_Stuff/2017-04-13-DOS_Timer_Stuff.html
 * https://groups.google.com/g/comp.os.msdos.programmer/c/wE4xEMcvPvY?pli=1
 */

#define PIT_FREQ_HZ 1193182ULL

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

void pit_delay_us(uint64_t us) {
    uint64_t clocks_needed;
    uint16_t prev;

    clocks_needed =
        (us * (uint64_t)PIT_FREQ_HZ + 999999ULL) / 1000000ULL;

    prev = pit_read();

    while (clocks_needed != 0) {
        uint16_t cur, counter_delta;
        uint64_t clocks_elapsed;

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
        clocks_elapsed = (uint64_t)counter_delta / 2ULL;

        if (clocks_elapsed > clocks_needed)
            clocks_elapsed = clocks_needed;

        clocks_needed -= clocks_elapsed;
        prev = cur;
    }
}
