#ifndef VSYNC_H
#define VSYNC_H

#include <stdint.h>

typedef struct {
    unsigned long frames;
    unsigned long bad;
    uint32_t      total_clk;
} vsync_stats_t;

/* Measure the frame period, needed for the interrupt window. 
 * 0 if no VGA retrace.
 * Needs pit_init() */
int vsync_calibrate(double *hz);

/*
 * Locks to the VGA vertical retrace. 
 * During playback we disable interrupts for ~2 ms before each
 * retrace until the next vsync_wait() so no ISR can, well, interrupt.
 * vsync_begin() and vsync_wait() return right after a retrace, with
 * interrupts disabled until the next wait or vsync_end() */
void vsync_begin();
void vsync_wait();
void vsync_end(vsync_stats_t *stats);

void vsync_print_stats(const vsync_stats_t *stats, unsigned long expected);

#endif
