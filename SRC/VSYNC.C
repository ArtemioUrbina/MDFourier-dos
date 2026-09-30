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

static uint32_t      period_clk;
static double        period_exact;
static uint32_t      last_edge;
static vsync_stats_t stats;

static void wait_edge();
static int  wait_edge_timeout(uint32_t timeout);

int vsync_calibrate(double *hz) {
    uint32_t start, end, total;
    unsigned i;

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

    _disable();
    wait_edge();
    last_edge = pit_now();
}

void vsync_wait() {
    uint32_t t, delta;

    /* Interrupts on until ~2 ms before the retrace */
    _enable();
    while ((uint32_t)(pit_now() - last_edge) < period_clk - WINDOW_CLK)
        ;
    _disable();

    wait_edge();
    t = pit_now();

    /* Frame check */
    delta = t - last_edge;
    last_edge = t;
    stats.frames++;
    stats.total_clk += delta;
    if (delta + FRAME_TOL_CLK < period_clk || delta  > period_clk + FRAME_TOL_CLK)
        stats.bad++;
}

/* Start of the next retrace. */
static void wait_edge() {
    while (inp(VGA_STATUS_PORT) & VGA_VRETRACE)
        ;   /* let any retrace already in progress finish */

    while (!(inp(VGA_STATUS_PORT) & VGA_VRETRACE))
        ;   /* wait for the next one to start */
}

/* VGA detection */
static int wait_edge_timeout(uint32_t timeout) {
    uint32_t start = pit_now();

    while (inp(VGA_STATUS_PORT) & VGA_VRETRACE)
        if ((uint32_t)(pit_now() - start) > timeout)
            return 0;
    while (!(inp(VGA_STATUS_PORT) & VGA_VRETRACE))
        if ((uint32_t)(pit_now() - start) > timeout)
            return 0;
    return 1;
}

void vsync_end(vsync_stats_t *out) {
    _enable();
    *out = stats;
}

void vsync_print_stats(const vsync_stats_t *s, unsigned long expected) {
    printf("  frames: %lu expected, %lu played, %lu bad\n",
           expected, s->frames, s->bad);
    printf("  length: %.4fs measured, %.4fs expected\n",
           (double)s->total_clk/PIT_HZ_F,
           (double)expected*period_exact/PIT_HZ_F);
    if (s->bad)
        printf("  WARNING: %lu bad frame(s), discard this capture\n",
               s->bad);
}
