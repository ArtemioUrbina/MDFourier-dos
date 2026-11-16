#ifndef VSYNC_H
#define VSYNC_H

#include <stdint.h>

typedef struct {
    unsigned long frames;
    unsigned long bad;              /* out of tolerance */
    uint32_t      total_clk;
    /* Tolerant mode */
    unsigned long missed;           /* edge passed while preempted, caught up */
    unsigned long over_1ms;         /* bad frames more than 1 ms off */
    uint32_t      worst_clk;        /* the most a frame was off */
} vsync_stats_t;

/* Measures the frame period, 0 if no retrace or rate out of range.
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
unsigned long vsync_first_bad();

/* Under Windows the VM delays the retrace. Tolerant mode counts a frame
 * whose edge passed as played and catches up, bad frames only counted */
void vsync_set_tolerant(int on);
void vsync_diagnose();

void vsync_print_stats(vsync_stats_t *stats, unsigned long expected);

#endif
