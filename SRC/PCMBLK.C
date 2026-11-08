#include "pcmblk.h"
#include "seq.h"

/* One transfer, DMA loops the buffer */
void pcmblk_clock(unsigned frames) {
    if (seq_stopped())
        return;

    seq_add_frames(frames);
    pcm_clock_start();
    while (frames-- && seq_frame())
        ;
    pcm_clock_stop();
}

/* Each step starts on its frame edge and queues the next one */
void pcmblk_steps(pcm_steps_t kind, unsigned steps, unsigned frames) {
    unsigned step, frame;

    for (step = 0; step < steps && !seq_stopped(); step++) {
        seq_add_frames(frames);
        pcm_step_start(kind, step & 1);
        if (step + 1 < steps)
            pcm_step_fill(kind, step + 1, (step + 1) & 1, frames);
        for (frame = 0; frame < frames; frame++)
            if (!seq_frame())
                break;
    }
    pcm_steps_end(kind);
}

/* The same for ADPCM, the DSP decodes it */
void pcmblk_adpcm_clock(unsigned frames) {
    if (seq_stopped())
        return;

    seq_add_frames(frames);
    pcm_adpcm_clock_start();
    while (frames-- && seq_frame())
        ;
    pcm_adpcm_clock_stop();
}

void pcmblk_adpcm_steps(pcm_adpcm_t format, pcm_steps_t kind, unsigned steps, unsigned frames) {
    unsigned step, frame;

    for (step = 0; step < steps && !seq_stopped(); step++) {
        seq_add_frames(frames);
        pcm_adpcm_step_start(format, kind, step & 1);
        if (step + 1 < steps)
            pcm_adpcm_step_fill(format, kind, step + 1, (step + 1) & 1, frames);
        for (frame = 0; frame < frames; frame++)
            if (!seq_frame())
                break;
    }
    pcm_adpcm_steps_end(kind);
}
