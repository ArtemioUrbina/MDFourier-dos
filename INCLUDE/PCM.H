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

/* ---- Stereo and 16-bit ----
 * Played only when supported
 * 8-bit stereo on DSP 3.xx and 4.xx
 * 16-bit stereo on DSP 4.xx with BLASTER H
 * Elsewhere the blocks stay silent and the sequence keeps its timing */

#define PCMX_SWEEP_STEPS 64     /* 25 Hz to 20 kHz */
#define PCMX_LEVEL_STEPS 8      /* 0 to -84 dB in 12 dB steps */
#define PCMX_SEP_STEPS   3      /* left, right, both */

typedef enum {
    PCMX_STEREO,                /* 8-bit stereo */
    PCMX_16                     /* 16-bit stereo */
} pcmx_section_t;

typedef enum {
    PCMX_CLK8,
    PCMX_SEP8,
    PCMX_CLK16,
    PCMX_SWEEP16,
    PCMX_LEVEL16,
    PCMX_SEP16,
    PCMX_KIND_COUNT
} pcmx_kind_t;

void pcmx_begin();
void pcmx_step_fill(pcmx_kind_t kind, unsigned step);
void pcmx_step_start(pcmx_kind_t kind);
void pcmx_steps_end(pcmx_kind_t kind);
int st_enabled(pcmx_kind_t kind);

void pcm_finish();
void pcm_print_report();

#endif
