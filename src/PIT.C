#include <conio.h>
#include "pit.h"

#define PIT_FREQ_HZ 1193182UL

#define PIT_CMD 0x43
#define PIT_CH0 0x40

uint16_t pit_read() {
    uint16_t lo, hi;

    outp(PIT_CMD, 0x00);
    lo = (uint16_t)inp(PIT_CH0);
    hi = (uint16_t)inp(PIT_CH0);
    return (uint16_t)((hi << 8) | lo);
}

void pit_delay_us(uint64_t us) {
    uint64_t clocks_needed;
    uint16_t prev;

    clocks_needed = (us * PIT_FREQ_HZ + 999999ULL) / 1000000ULL;
    prev = pit_read();

    while (clocks_needed != 0) {
        uint16_t cur, counter_delta;
        uint64_t clocks_elapsed;

        cur = pit_read();

        counter_delta = (uint16_t)(prev - cur);

        // In mode 3, 2 counter units ~= 1 PIT input clock.
        clocks_elapsed = counter_delta / 2;

        if (clocks_elapsed > clocks_needed)
            clocks_elapsed = clocks_needed;

        clocks_needed -= clocks_elapsed;
        prev = cur;
    }
}
