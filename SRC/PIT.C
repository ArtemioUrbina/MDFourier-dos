#include <conio.h>
#include "pit.h"
#include "irq.h"

/*
 * https://expiredpopsicle.com/articles/2017-04-13-DOS_Timer_Stuff/2017-04-13-DOS_Timer_Stuff.html
 * https://groups.google.com/g/comp.os.msdos.programmer/c/wE4xEMcvPvY?pli=1
 */

/* counter 0, lo/hi byte, mode 3, binary */
#define PIT_CH0_MODE3   0x36

static int      tb_started = 0;
static uint16_t tb_last;
static uint16_t tb_rem;
static uint32_t tb_clocks;

#define PIT_CMD 0x43
#define PIT_CH0 0x40

/* Needs intrrupts disabled */
static uint16_t pit_read() {
    uint16_t lo, hi;

    /* latch command, counter 0 */
    outp(PIT_CMD, 0x00);
    lo = (uint16_t)inp(PIT_CH0);
    hi = (uint16_t)inp(PIT_CH0);
    return (uint16_t)((hi << 8) | lo);
}

void pit_delay_us(uint32_t us) {
    uint32_t clocks = PIT_US_TO_CLK(us) + 1UL;
    uint32_t start  = pit_now();

    while ((uint32_t)(pit_now() - start) < clocks)
        ;
}

uint32_t pit_now() {
    unsigned flags;
    uint16_t cur, delta;
    uint32_t units;

    flags = irq_save();

    cur = pit_read();
    if (!tb_started) {
        tb_last    = cur;
        tb_rem     = 0;
        tb_started = 1;
    }

    /* counts down, 16-bit subtraction handles the wrap */
    delta   = (uint16_t)(tb_last - cur);
    tb_last = cur;

    /* mode 3 counts by 2, keep the odd remainder */
    units      = (uint32_t)delta + tb_rem;
    tb_clocks += units >> 1;
    tb_rem     = (uint16_t)(units & 1u);

    irq_restore(flags);
    return tb_clocks;
}


/* Set channel 0 to mode 3, reload 65536 (BIOS default) */
void pit_init() {
    unsigned flags;

    flags = irq_save();
    outp(PIT_CMD, PIT_CH0_MODE3);
    outp(PIT_CH0, 0x00);
    outp(PIT_CH0, 0x00);
    tb_started = 0;
    irq_restore(flags);
}
