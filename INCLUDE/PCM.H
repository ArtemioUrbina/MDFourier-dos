#ifndef PCM_H
#define PCM_H

#include <stdint.h>
#include "env.h"

/* 8-bit mono PCM, DSP 1.xx and up, 22222.2 Hz nominal (1 MHz / 45) */

/* Sweep and level steps */
#define PCM_SWEEP_STEPS 64      /* 20 Hz to 10 kHz, log spaced */
#define PCM_LEVEL_STEPS 8       /* 0 to -42 dB in 6 dB steps */

typedef enum {
    PCM_SWEEP,
    PCM_LEVEL
} pcm_steps_t;

/* Checks BLASTER A, I and D, resets the DSP, builds the tables and
 * installs the IRQ handler (removed at exit). 1 if PCM can play.
 * frame_hz sizes the tones to the blocks. extended: also sets up the
 * stereo and 16-bit ones, otherwise only 8-bit mono and ADPCM play */
int  pcm_init(blaster_cfg_t *cfg, double frame_hz, int extended);
/* pcm_init() saw the DSP raise another IRQ than BLASTER I: said so, stop */
int  pcm_irq_mismatch();

/* Speaker on and the rate. At the start of the gap before the first
 * PCM block: the SB 1.x/2.0 speaker takes up to ~112 ms */
void pcm_prepare();

/* Clock tone, rate / 8, repeated by DMA for most of a block.
 * Arm in the last frame of the gap, start on the edge and
 * pcm_clock_stop() after the block */
void pcm_clock_arm(unsigned frames);
void pcm_clock_start();
void pcm_clock_stop();

/* One tone per step, single-cycle. Queue a step frames before it
 * plays, start each on a frame edge. buf alternates 0/1 */
void pcm_step_fill(pcm_steps_t kind, unsigned step, unsigned buf, unsigned frames);
void pcm_step_start(pcm_steps_t kind, unsigned buf);

/* Builds the queued step a chunk at a time, call once per frame */
void pcm_fill_more();
void pcm_steps_end(pcm_steps_t kind);

/* ---- Stereo and 16-bit ----
 * Played only where the card can: 8-bit stereo on DSP 3.xx and 4.xx,
 * 16-bit stereo on DSP 4.xx with BLASTER H, on ESS ES18xx in Extended
 * mode and through the WSS codec of the OPL3-SAx. Elsewhere MDF.C
 * plays a filler and the sequence keeps its timing */

#define PCMX_SWEEP_STEPS 64     /* 25 Hz to 20 kHz */
#define PCMX_LEVEL_STEPS 8      /* 0 to -84 dB in 12 dB steps */
#define PCMX_SEP_STEPS   3      /* left, right, both */

typedef enum {
    PCMX_STEREO,                /* 8-bit stereo */
    PCMX_16                     /* 16-bit stereo */
} pcmx_section_t;

/* What a start plays. The clocks (rate / 8, ~0.74 s) need no queuing */
typedef enum {
    PCMX_CLK8,
    PCMX_SEP8,
    PCMX_CLK16,
    PCMX_SWEEP16,
    PCMX_LEVEL16,
    PCMX_SEP16,
    PCMX_KIND_COUNT
} pcmx_kind_t;

/* Right after the previous block, before the stereo section's gap.
 * Sets up the SB Pro method on DSP 3.xx */
void pcmx_begin();
/* Right after the 8-bit stereo section, before the 16-bit one's gap.
 * The 16-bit driver's setup: ESS Extended mode, the WSS codec */
void pcmx_begin16();
/* Queue each step at least a few frames before it plays */
void pcmx_step_fill(pcmx_kind_t kind, unsigned step);
/* On a frame edge. Counts the IRQ of what played before */
void pcmx_step_start(pcmx_kind_t kind);
/* After the block of the last start */
void pcmx_steps_end(pcmx_kind_t kind);
/* 1 when this card plays that kind, otherwise its blocks are silent */
int pcmx_enabled(pcmx_kind_t kind);

/* ---- ADPCM ----
 * Creative's 4, 2.6 and 2-bit ADPCM at 10000 Hz, decoded by the DSP to
 * 8-bit mono. Every Creative DSP from 1.xx to 4.xx, clones may not */

#define PCM_ADPCM_SWEEP_STEPS   16      /* 50 Hz to 4 kHz */
#define PCM_ADPCM_LEVEL_STEPS   8       /* 0 to -42 dB in 6 dB steps */

typedef enum {
    PCM_ADPCM4,
    PCM_ADPCM3,                 /* 2.6-bit: three samples in a byte */
    PCM_ADPCM2
} pcm_adpcm_t;

/* After pcm_init(), before the sequence: 1 if the DSP takes ADPCM.
 * Without it the ADPCM calls below do nothing */
int  pcm_adpcm_probe();

/* Clock tone, 4-bit at rate / 8. Built from the start of the silence
 * before it, a chunk per frame; armed in its last frame, started on
 * the edge, pcm_adpcm_clock_stop() after the block */
void pcm_adpcm_clock_build(unsigned frames);
void pcm_adpcm_clock_arm();
void pcm_adpcm_clock_start();
void pcm_adpcm_clock_stop();

/* As the 8-bit steps: queue a step frames before it plays, buf
 * alternates 0/1 */
void pcm_adpcm_step_fill(pcm_adpcm_t format, pcm_steps_t kind, unsigned step, unsigned buf, unsigned frames);
void pcm_adpcm_step_start(pcm_adpcm_t format, pcm_steps_t kind, unsigned buf);
void pcm_adpcm_steps_end(pcm_steps_t kind);

/* After the sequence: speaker off, DSP reset if anything was left
 * playing. Both click, kept out of the Silence block */
void pcm_finish();

void pcm_print_report();

#endif
