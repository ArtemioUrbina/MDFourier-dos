/*
 * VGA vertical retrace detection.
 *
 * Port 0x3DA, bit 3 (0x08) of the Input Status Register 1 is set
 * for the duration of the vertical retrace pulse.
 *
 */

#include <stdio.h>
#include <conio.h>
#include <i86.h>
#include "vsync.h"
#include "pit.h"

#define VGA_STATUS_PORT 0x3DA
#define VGA_VRETRACE    0x08

/* Interrupts off before the retrace.
 * Longest ISR in tests was ~290 us measured
 * on a 486@66Mhz with EMM386 this will probably
 * change in the future */
#define WINDOW_CLK      PIT_US_TO_CLK(2000UL)

/* Frame tolerance */
#define FRAME_TOL_CLK   PIT_US_TO_CLK(250UL)

/* VGA detection */
#define EDGE_TIMEOUT    PIT_US_TO_CLK(100000UL)   
#define CALIB_FRAMES    16

uint32_t      period_clk;
double        period_exact;
uint32_t      last_edge;
vsync_stats_t stats;

unsigned long first_bad_frame;
int           tolerant;

void vsync_set_tolerant(int on) {
    tolerant = on;
}

/* Out of tolerance by off_clk */
void count_bad(uint32_t off_clk) {
    if (!stats.bad)
        first_bad_frame = stats.frames;
    stats.bad++;
    if (off_clk > PIT_US_TO_CLK(1000UL))
        stats.over_1ms++;
    if (off_clk > stats.worst_clk)
        stats.worst_clk = off_clk;
}
void wait_edge();
int  wait_edge_timeout(uint32_t timeout);

int vsync_calibrate(double *hz) {
    uint32_t start, end, total;
    unsigned i;

    *hz = 0;
    if (!wait_edge_timeout(EDGE_TIMEOUT))
        return 0;

    /* Interrupts off  */
    _disable();
    wait_edge();
    start = pit_now();
    for (i = 0; i < CALIB_FRAMES; i++) {
        wait_edge();
        end = pit_now();            /* same point after each edge */
    }
    _enable();
    total = end - start;

    period_exact = (double)total / CALIB_FRAMES;
    period_clk = (total + CALIB_FRAMES / 2) / CALIB_FRAMES;
    *hz = PIT_HZ_F * CALIB_FRAMES / (double)total;

    /* Windows DOS box gives nonsense */
    return (*hz >= 30.0 && *hz <= 120.0);
}

void vsync_begin() {
    stats.frames    = 0;
    stats.bad       = 0;
    stats.total_clk = 0;
    stats.missed    = 0;
    stats.over_1ms  = 0;
    stats.worst_clk = 0;

    _disable();
    wait_edge();
    last_edge = pit_now();
}

void vsync_wait() {
    uint32_t t, delta, elapsed;

    /* Interrupts on until ~2 ms before the retrace */
    _enable();
    for (;;) {
        elapsed = pit_now() - last_edge;
        /* Tolerant: preempted past the edge, this frame is over */
        if (tolerant && elapsed > period_clk + FRAME_TOL_CLK) {
            _disable();
            last_edge += period_clk;
            stats.frames++;
            stats.total_clk += period_clk;
            stats.missed++;
            count_bad(elapsed - period_clk);
            return;
        }
        if (elapsed >= period_clk - WINDOW_CLK)
            break;
    }
    _disable();

    wait_edge();
    t = pit_now();

    /* Frame check */
    delta = t - last_edge;
    last_edge = t;
    stats.frames++;

    /* When in tolerantmode, an edge went by while waiting, at the next one now.
     * Back to the first, the next waits return at once for the others */
    if (tolerant && delta > period_clk + period_clk / 2) {
        uint32_t edges = (delta + period_clk / 2) / period_clk;

        last_edge = t - (edges - 1) * period_clk;
        stats.total_clk += period_clk;
        stats.missed++;
        count_bad(delta - period_clk);
        return;
    }

    stats.total_clk += delta;
    if (delta + FRAME_TOL_CLK < period_clk || delta  > period_clk + FRAME_TOL_CLK)
        count_bad(delta > period_clk ? delta - period_clk : period_clk - delta);
}

/* Start of the next retrace. */
void wait_edge() {
    while (inp(VGA_STATUS_PORT) & VGA_VRETRACE)
        ;   /* let any retrace already in progress finish */

    while (!(inp(VGA_STATUS_PORT) & VGA_VRETRACE))
        ;   /* wait for the next one to start */
}

/* VGA detection */
int wait_edge_timeout(uint32_t timeout) {
    uint32_t start = pit_now();

    while (inp(VGA_STATUS_PORT) & VGA_VRETRACE)
        if ((uint32_t)(pit_now() - start) > timeout)
            return 0;
    while (!(inp(VGA_STATUS_PORT) & VGA_VRETRACE))
        if ((uint32_t)(pit_now() - start) > timeout)
            return 0;
    return 1;
}

/* BIOS tick count, 18.2 Hz from the timer interrupt.
 * When in Windows, a second clock to check the PIT reads and the retrace against */
#define BDA_TICKS       (*(volatile uint32_t __far *)MK_FP(0x40, 0x6C))
#define DIAG_GUARD      20000000UL      /* loop limit if the ticks stop */

int wait_tick() {
    uint32_t      tick = BDA_TICKS;
    unsigned long guard;

    for (guard = 0; guard < DIAG_GUARD; guard++)
        if (BDA_TICKS != tick)
            return 1;
    return 0;
}

void vsync_diagnose() {
    uint32_t      tick, pit_start, pit_end;
    unsigned long reads = 0, high = 0, edges = 0, guard;
    int           level, last;

    _enable();
    printf(" Timing check:\n");
    if (!wait_tick()) {
        printf("   BIOS timer ticks don't advance, can't check\n");
        return;
    }

    /* PIT clocks per BIOS tick: 65536 when the PIT reads are real */
    tick = BDA_TICKS;
    pit_start = pit_now();
    for (guard = 0; BDA_TICKS - tick < 9 && guard < DIAG_GUARD; guard++)
        pit_now();
    pit_end = pit_now();
    printf("   Clock: %.0f PIT clocks per BIOS tick (65536 expected)\n",
           (double)(uint32_t)(pit_end - pit_start) / (double)(BDA_TICKS - tick));

    /* Retrace edges in 18 ticks, ~0.99 s */
    wait_tick();
    tick = BDA_TICKS;
    last = inp(VGA_STATUS_PORT) & VGA_VRETRACE;
    for (guard = 0; BDA_TICKS - tick < 18 && guard < DIAG_GUARD; guard++) {
        level = inp(VGA_STATUS_PORT) & VGA_VRETRACE;
        reads++;
        if (level)
            high++;
        if (level && !last)
            edges++;
        last = level;
    }
    printf("   Retrace: %lu edges, %.2f Hz by BIOS ticks, in retrace %.1f%% of %lu reads\n",
           edges, edges * 1193181.67 / 65536.0 / 18.0,
           reads ? high * 100.0 / reads : 0.0, reads);
}

unsigned long vsync_first_bad() {
    return stats.bad ? first_bad_frame : 0;
}

void vsync_end(vsync_stats_t *out) {
    _enable();
    *out = stats;
}

void vsync_print_stats(vsync_stats_t *s, unsigned long expected) {
    printf("  frames: %lu expected, %lu played, %lu bad\n",
           expected, s->frames, s->bad);
    printf("  length: %0.4fs measured, %0.4fs expected\n",
           (double)s->total_clk/PIT_HZ_F,
           (double)expected*period_exact/PIT_HZ_F);
    /* More precision, using the whole sequence */
    if (!s->bad && s->total_clk) {
        double hz = PIT_HZ_F * (double)s->frames / (double)s->total_clk;
        printf("  rate: %0.4fHz (%0.4fms per frame)\n",
               hz, 1000.0f/hz);
    }
    if (s->bad && tolerant) {
        printf("  timing: %lu late, %lu by more than 1 ms, worst %.2f ms, %lu missed and caught up\n",
               s->bad, s->over_1ms, s->worst_clk * 1000.0 / PIT_HZ_F, s->missed);
        printf("  NOTE: Windows timing, the capture lines up less precisely\n");
    } else if (s->bad)
        printf("  WARNING: %lu bad frame(s), discard this capture\n",
               s->bad);
}
