#include <stdio.h>
#include <conio.h>
#include <dos.h>
#include <i86.h>
#include "pit.h"
#include "irq.h"

/*
 * https://expiredpopsicle.com/articles/2017-04-13-DOS_Timer_Stuff/2017-04-13-DOS_Timer_Stuff.html
 * https://groups.google.com/g/comp.os.msdos.programmer/c/wE4xEMcvPvY?pli=1
 */

/* counter 0, lo/hi byte, mode 3, binary */
#define PIT_CH0_MODE3   0x36

/* Time stamp counter in use, its PIT clocks per count and state */
int      tsc_used;
double   tsc_hz;
double   tsc_to_pit;
double   tsc_frac;
uint32_t tsc_last;

int      tb_started = 0;
uint16_t tb_last;
uint16_t tb_rem;
uint32_t tb_clocks;

/* Windows' PIT: counting by one instead of two, or scaled to the BIOS ticks when neither fits */
int      tb_by_one;
double   tb_scale = 1.0;
double   tb_sfrac;
double   tb_per_tick;
char    *tb_mode = "";

#define PIT_CMD 0x43
#define PIT_CH0 0x40

/* Needs intrrupts disabled */
uint16_t pit_read() {
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

/* For 386 and up only. We use this under Windows.
 * Raw bytes since the compiler targets the 8086.
 * CPUID 1, EDX bit 4: RDTSC */
int cpu_has_tsc();
#pragma aux cpu_has_tsc = \
    0x66 0x53                       /* push ebx */          \
    0x66 0xB8 0x01 0x00 0x00 0x00   /* mov eax, 1 */        \
    0x0F 0xA2                       /* cpuid */             \
    0x66 0x5B                       /* pop ebx */           \
    0x8B 0xC2                       /* mov ax, dx */        \
    0xC1 0xE8 0x04                  /* shr ax, 4 */         \
    0x25 0x01 0x00                  /* and ax, 1 */         \
    value [ax] modify exact [ax cx dx];

/* Low 32 bits of the time stamp counter */
uint32_t tsc_read();
#pragma aux tsc_read = \
    0x0F 0x31                       /* rdtsc */             \
    0x66 0x8B 0xD0                  /* mov edx, eax */      \
    0x66 0xC1 0xEA 0x10             /* shr edx, 16 */       \
    value [dx ax] modify exact [ax dx];

/* CPUID is tried directly: the EFLAGS ID test can't be trusted in a Windows DOS box, its POPF is emulated.
 * Without CPUID (a 486) the invalid opcode comes back as INT 6 and the handler steps over it */
volatile int ud_hit;

void __interrupt __far ud_handler(union INTPACK r) {
    ud_hit = 1;
    r.w.ip += 2;                    /* the 2-byte CPUID */
}

/* BIOS ticks are 18.2 Hz, the reference the counter is measured against.
 * Under Windows they come in bursts at times, over ~2 s that is within ~0.5% */
#define BDA_TICKS       (*(volatile uint32_t __far *)MK_FP(0x40, 0x6C))
#define TICKS_PER_SEC   (1193181.67 / 65536.0)
#define CAL_TICKS       36
#define TICK_GUARD      100000000UL     /* reads before giving up */

int bios_next_tick() {
    uint32_t      tick = BDA_TICKS;
    unsigned long guard;

    for (guard = 0; guard < TICK_GUARD; guard++)
        if (BDA_TICKS != tick)
            return 1;
    return 0;
}

char *tsc_failure = "not tried";
char  tsc_text[48];

int pit_use_tsc() {
    void (__interrupt __far *old_ud)();
    uint32_t start;
    int      has, ticks;

    ud_hit = 0;
    old_ud = _dos_getvect(6);
    _dos_setvect(6, ud_handler);
    has = cpu_has_tsc();
    _dos_setvect(6, old_ud);
    if (ud_hit || !has) {
        tsc_failure = ud_hit ? "no CPUID" : "CPUID says no time stamp counter";
        return 0;
    }

    _enable();
    if (!bios_next_tick()) {
        tsc_failure = "BIOS ticks don't move";
        return 0;
    }
    start = tsc_read();
    for (ticks = 0; ticks < CAL_TICKS; ticks++)
        if (!bios_next_tick()) {
            tsc_failure = "BIOS ticks stopped";
            return 0;
        }
    tsc_hz = (double)(uint32_t)(tsc_read() - start) * TICKS_PER_SEC / CAL_TICKS;

    /* Below a Pentium's, the count went wrong */
    if (tsc_hz < 50000000.0) {
        sprintf(tsc_text, "counter at %.2f MHz, too slow", tsc_hz / 1000000.0);
        tsc_failure = tsc_text;
        return 0;
    }

    tsc_to_pit = PIT_HZ_F / tsc_hz;
    tsc_frac   = 0;
    tsc_last   = tsc_read();
    tsc_used   = 1;
    return 1;
}

char *pit_tsc_failure() {
    return tsc_failure;
}

double pit_tsc_hz() {
    return tsc_used ? tsc_hz : 0;
}

uint32_t pit_now() {
    unsigned flags;
    uint16_t cur, delta;
    uint32_t units;

    /* Wraps every 2^32 counts, 4.3s at 1 GHz: always called sooner */
    if (tsc_used) {
        uint32_t now = tsc_read();
        uint32_t whole;

        tsc_frac += (double)(uint32_t)(now - tsc_last) * tsc_to_pit;
        tsc_last  = now;
        whole     = (uint32_t)tsc_frac;
        tsc_frac -= whole;
        tb_clocks += whole;
        return tb_clocks;
    }

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

    /* mode 3 counts by 2, keep the odd remainder. Windows 3.x's by 1 */
    if (tb_by_one)
        tb_clocks += delta;
    else if (tb_scale == 1.0) {
        units      = (uint32_t)delta + tb_rem;
        tb_rem     = (uint16_t)(units & 1u);
        tb_clocks += units >> 1;
    } else {
        uint32_t whole;

        units      = (uint32_t)delta + tb_rem;
        tb_rem     = (uint16_t)(units & 1u);
        tb_sfrac  += (double)(units >> 1) * tb_scale;
        whole      = (uint32_t)tb_sfrac;
        tb_sfrac  -= whole;
        tb_clocks += whole;
    }

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
    tb_scale   = 1.0;
    tb_by_one  = 0;
    irq_restore(flags);
}

/* Windows 3.x's virtual PIT counts by one, not by two as mode 3 does:
 * half the clocks per BIOS tick.
 * Its count is right, the ticks are the ones that lose time there, ~1.7%.
 * Only when the count is something else, it's scaled to the ticks. */
int pit_check_rate() {
    uint32_t start, end;
    int      ticks;

    _enable();
    if (!bios_next_tick())
        return 0;
    start = pit_now();
    for (ticks = 0; ticks < CAL_TICKS; ticks++) {
        uint32_t      tick = BDA_TICKS;
        unsigned long guard;

        for (guard = 0; BDA_TICKS == tick; guard++) {
            pit_now();
            if (guard >= TICK_GUARD)
                return 0;
        }
    }
    end = pit_now();

    tb_per_tick = (double)(uint32_t)(end - start) / CAL_TICKS;
    /* Not a counter that moves at a steady rate */
    if (tb_per_tick < 6553.6 || tb_per_tick > 655360.0)
        return 0;

    if (tb_per_tick > 65536.0 * 0.45 && tb_per_tick < 65536.0 * 0.55) {
        tb_by_one = 1;
        tb_mode   = "counts by one, read as such";
    } else if (tb_per_tick > 65536.0 * 0.95 && tb_per_tick < 65536.0 * 1.05)
        tb_mode   = "real rate";
    else {
        tb_scale  = 65536.0 / tb_per_tick;
        tb_sfrac  = 0;
        tb_mode   = "scaled to 65536";
    }
    return 1;
}

char *pit_rate_mode() {
    return tb_mode;
}

double pit_ticks_measured() {
    return tb_per_tick;
}
