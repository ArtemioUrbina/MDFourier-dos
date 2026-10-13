#ifndef PCM_H
#define PCM_H

#include <stdint.h>
#include "env.h"

/* 8-bit mono PCM, DSP 1.xx and up, 22222.2 Hz nominal (1 MHz / 45) */

#define PCM_SWEEP_STEPS 64      /* 20 Hz to 10 kHz, log spaced */
#define PCM_LEVEL_STEPS 8       /* 0 to -42 dB in 6 dB steps */

typedef enum {
    PCM_SWEEP,
    PCM_LEVEL
} pcm_steps_t;

int pcm_init(blaster_cfg_t *cfg, double frame_hz);
void pcm_prepare();
void pcm_clock_arm(unsigned frames);
void pcm_clock_start();
void pcm_clock_frame();
void pcm_clock_stop();

void pcm_step_fill(pcm_steps_t kind, unsigned step, unsigned buf, unsigned frames);
void pcm_step_start(pcm_steps_t kind, unsigned buf);

void pcm_fill_more();
void pcm_steps_end(pcm_steps_t kind);

void pcm_finish();
void pcm_print_report();

#endif
