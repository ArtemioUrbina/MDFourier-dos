#include <stdio.h>
#include "seq.h"
#include "vsync.h"
#include "kbd.h"
#include "host.h"

int             seq_aborted;
int             seq_late_stop;
unsigned long   seq_frames;
void            (*seq_frame_hook)();
vsync_stats_t   seq_stats;

void seq_begin(void (*frame_hook)()) {
    seq_aborted    = 0;
    seq_late_stop  = 0;
    seq_frames     = 0;
    seq_frame_hook = frame_hook;

    kbd_poll_escape();
    printf("Press ESC to abort\n");

    vsync_set_tolerant(host_is_windows());
    /* Align the first start to a frame edge */
    vsync_begin();
}

/* A late frame stops the run, under Windows we only count it */
int seq_check() {
    if (!seq_aborted && !host_is_windows() && vsync_first_bad()) {
        seq_aborted   = 1;
        seq_late_stop = 1;
    } 
    if (!seq_aborted && kbd_poll_escape())
        seq_aborted = 1;
    return seq_aborted;
}

int seq_stopped() {
    return seq_aborted;
}

int seq_frame() {
    if (seq_check())
        return 0;
    if (seq_frame_hook)
        seq_frame_hook();
    vsync_wait();
    return 1;
}

void seq_add_frames(unsigned frames) {
    seq_frames += frames;
}

void seq_silence(unsigned frames) {
    seq_frames += frames;
    while (frames-- && seq_frame())
        ;
}

void seq_block(int steps, unsigned frames, seq_step_fn prep, seq_step_fn on,
               seq_step_fn off, void *context) {
    unsigned frame, off_frame = frames - frames / 5;
    int      step;

    for (step = 0; step < steps && !seq_aborted; step++) { 
        on(context, step);
        seq_frames += frames;
        for (frame = 0; frame < frames; frame++) {
            if (frame == off_frame) {
                if (off)
                    off(context, step);
                if (prep && step + 1 < steps)
                    prep(context, step + 1);
            }
            if (!seq_frame())
                return;
        }
    }
}

void seq_stop() {
    vsync_end(&seq_stats);
}

int seq_stopped_late() {
    return seq_late_stop;
}

int seq_report() {
    unsigned long total = seq_frames;

    if (seq_late_stop) {
        printf("\nStopped at frame %lu, it ran late: this CPU is too slow.\n"
               "  MDF needs a 486SX with its internal cache enabled, in an\n"
               "  emulator raise the cycles. Discard this capture\n", vsync_first_bad());
        return SEQ_BAD_FRAMES;
    }
    if (seq_aborted) {
        printf("\nAborted\n");
        total = seq_stats.frames;
    } else
        printf("\nSequence complete: %lu frames\n", total);

    vsync_print_stats(&seq_stats, total);
    if (seq_aborted)
        return SEQ_ABORTED;
    return seq_stats.bad ? SEQ_BAD_FRAMES : SEQ_OK;
}
