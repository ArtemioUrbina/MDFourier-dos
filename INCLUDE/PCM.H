#ifndef PCM_H
#define PCM_H

#include <stdint.h>
#include "env.h"

/* 8-bit mono, DSP 1.xx and up, 22222.2 Hz (1 MHz / 45). High-speed,
 * DSP 2.01 and up, 43478.3 Hz (1 MHz / 23) */

/* Sweep and level steps */
#define PCM_SWEEP_STEPS 64      /* 20 Hz to 10 kHz, log spaced */
#define PCM_LEVEL_STEPS 8       /* 0 to -42 dB in 6 dB steps */
#define PCM_HS_SWEEP_STEPS 64   /* high-speed, 20 Hz to 20 kHz */
/* Upper half of each sweep again with the SB Pro filter off, MDFourier
 * overlays them */
#define PCM_NF_STEPS    32

typedef enum {
    PCM_SWEEP,
    PCM_LEVEL,
    PCM_HS_SWEEP,               /* high-speed mode, main sequence only */
    PCM_SWEEP_NF,               /* the upper PCM_NF_STEPS of each sweep */
    PCM_HS_SWEEP_NF,
    PCM_STEPS_COUNT
} pcm_steps_t;

/* Checks BLASTER A, I, D, resets the DSP, builds the tables and installs
 * the IRQ handler. 1 if PCM plays. extended: stereo and 16-bit too */
int  pcm_init(blaster_cfg_t *cfg, double frame_hz, int extended);
/* pcm_init() saw the DSP raise another IRQ than BLASTER I */
int  pcm_irq_mismatch();

/* Speaker on and rate, at the start of the gap: the SB 1.x/2.0 speaker
 * takes up to ~112 ms */
void pcm_prepare();

/* Clock tone, rate / 8. Arm in the gap's last frame, start on the
 * edge, pcm_clock_stop() after the block */
void pcm_clock_arm(unsigned frames);
void pcm_clock_start();
void pcm_clock_stop();

/* 1 when the card plays high-speed */
int  pcm_hs_enabled();
void pcm_hs_clock_arm(unsigned frames);

/* One tone per step. Queue a step frames ahead, start on a frame edge,
 * buf alternates 0/1 */
void pcm_step_fill(pcm_steps_t kind, unsigned step, unsigned buf, unsigned frames);
void pcm_step_start(pcm_steps_t kind, unsigned buf);

/* Builds the queued step a chunk at a time, once per frame */
void pcm_fill_more();
void pcm_steps_end(pcm_steps_t kind);

/* ---- Stereo and 16-bit ----
 * 8-bit stereo on DSP 3.xx and 4.xx, 16-bit on DSP 4.xx with H, ESS
 * Extended mode and the OPL3-SAx WSS codec. Elsewhere a filler plays */

#define PCMX_SWEEP_STEPS 64     /* 25 Hz to 20 kHz */
#define PCMX_LEVEL_STEPS 8      /* 0 to -84 dB in 12 dB steps */
#define PCMX_SEP_STEPS   3      /* left, right, both */

typedef enum {
    PCMX_STEREO,                /* 8-bit stereo */
    PCMX_16                     /* 16-bit stereo */
} pcmx_section_t;

/* What a start plays. The clocks need no queuing */
typedef enum {
    PCMX_CLK8,
    PCMX_SEP8,
    PCMX_CLK16,
    PCMX_SWEEP16,
    PCMX_LEVEL16,
    PCMX_SEP16,
    PCMX_KIND_COUNT
} pcmx_kind_t;

/* Right after the previous block. SB Pro method on DSP 3.xx */
void pcmx_begin();
/* Right after the 8-bit stereo section, sets up the 16-bit driver */
void pcmx_begin16();
/* Queue each step a few frames ahead */
void pcmx_step_fill(pcmx_kind_t kind, unsigned step);
/* On a frame edge, counts the IRQ of the last one */
void pcmx_step_start(pcmx_kind_t kind);
/* After the block of the last start */
void pcmx_steps_end(pcmx_kind_t kind);
/* 1 when the card plays that kind, otherwise silent */
int pcmx_enabled(pcmx_kind_t kind);

/* ---- ADPCM ----
 * Creative's 4, 2.6 and 2-bit at 10000 Hz, every Creative DSP, clones
 * may not */

#define PCM_ADPCM_SWEEP_STEPS   16      /* 50 Hz to 4 kHz */
#define PCM_ADPCM_LEVEL_STEPS   8       /* 0 to -42 dB in 6 dB steps */

typedef enum {
    PCM_ADPCM4,
    PCM_ADPCM3,                 /* 2.6-bit: three samples in a byte */
    PCM_ADPCM2
} pcm_adpcm_t;

/* After pcm_init(): 1 if the DSP takes ADPCM, otherwise the calls below
 * do nothing */
int  pcm_adpcm_probe();

/* 4-bit clock tone, built a chunk per frame from the start of the
 * silence, armed in its last frame, started on the edge */
void pcm_adpcm_clock_build(unsigned frames);
void pcm_adpcm_clock_arm();
void pcm_adpcm_clock_start();
void pcm_adpcm_clock_stop();

/* As the 8-bit steps */
void pcm_adpcm_step_fill(pcm_adpcm_t format, pcm_steps_t kind, unsigned step, unsigned buf, unsigned frames);
void pcm_adpcm_step_start(pcm_adpcm_t format, pcm_steps_t kind, unsigned buf);
void pcm_adpcm_steps_end(pcm_steps_t kind);

/* Speaker off, DSP reset if something was left playing. Both click,
 * kept out of the Silence block */
void pcm_finish();

void pcm_print_report();

#endif
