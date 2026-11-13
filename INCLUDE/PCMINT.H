#ifndef PCMINT_H
#define PCMINT_H

/*
 * What PCM.C, PCMX.C and ADPCM.C share. The sequences use PCM.H, the
 * 16-bit drivers PCMDRV.H
 */

#include <stdint.h>
#include "pcm.h"
#include "pcmdrv.h"

#define DSP_READ_READY  0x0E    /* reading it acks the 8-bit IRQ */
#define DSP_ACK_16      0x0F    /* reading it acks the 16-bit IRQ, DSP 4.xx */
#define SB16_IRQ_STATUS 0x82    /* mixer: bit 0 8-bit, bit 1 16-bit */

#define DSP_PLAY_8BIT   0x14    /* single-cycle, 8-bit mono, every DSP */
#define DSP_HS_8BIT     0x91    /* high-speed 8-bit single-cycle, DSP 2.01 and up */
#define DSP_BLOCK_SIZE  0x48    /* its length, sent before it */

/* Rate = 1000000 / (256 - TC). TC 211 gives 22222.2 Hz, in range for
 * normal mode on every DSP version */
#define PCM_TC          211
#define PCM_RATE        (1000000.0 / 45.0)

/* High-speed mode: TC 233 gives 43478.3 Hz, over the ~23 kHz of normal
 * mode. The rate games used for 44 kHz mono on the SB 2.0 and SB Pro.
 * While a high-speed transfer plays the DSP takes no commands; a
 * single-cycle one goes back to normal mode when it ends. Every DSP
 * from 2.01 takes the same commands, the SB16 included, so all cards
 * play it the same way. The SB Pro output filter (mixer 0x0E) is on,
 * as MIXER.C sets it for all the 8-bit mono tones: its power-on state,
 * the one most games played through */
#define PCM_HS_TC       233
#define PCM_HS_RATE     (1000000.0 / 23.0)
#define HS_SWEEP_HIGH   20000.0

/* Tones fill this share of their block. The rest is silence, so a
 * tone is over before the next one starts even on the slowest card
 * measured, which plays 2.2% long. The silence is also a decay. */
#define FILL_PCT        85

/* Clock tone: rate / 8 (2777.8 Hz), below any card's output filter.
 * Its frequency measures the card's crystal: MDFourier reads it and
 * names the card family from it. Each 8-sample period ends on 0x80,
 * so the DAC rests at the midpoint whenever it stops (no click).
 * The block needs ~18000 samples, the buffer holds CLK_MAX.
 * DMA loops it in auto-init while the DSP plays one
 * single-cycle transfer of ~CLK_PCT of the block.  */
#define CLK_PERIOD      8
#define CLK_MAX         5600
#define CLK_PCT         95

/* Step tones. Level at rate / 22 so every period is the same 22
 * samples, no table jitter from one period to the next.
 * Single-cycle transfers start and stop at full level.  */
#define SWEEP_LOW       20.0
#define SWEEP_HIGH      10000.0
#define LEVEL_FREQ      (PCM_RATE / 22.0)   /* 1010.1 Hz */
#define FADE            32                  /* samples, in and out */

/* Sweep and clock 3 dB below full scale, so the first Level step
 * (full scale, 1 kHz) is always the loudest PCM element. MDFourier
 * normalizes to the loudest one, it must not depend on each card's
 * bass or treble response */
#define SWEEP_AMP       181                 /* of 256, -3.0 dB */
#define CLK_AMP         90.0                /* of 127, -3.0 dB */
#define STEP_MAX        5600                /* 20 frames at 85% */

/* Samples built per frame. A 20-frame step needs 8 frames, a 10-frame
 * one 4: a few ms per frame, even without an FPU */
#define FILL_CHUNK      768

/* ADPCM samples encoded per frame, each one a table lookup and the
 * decoder's step. A 60-frame clock takes 32 frames, a 20-frame step 10 */
#define ADPCM_CHUNK     256

/* Sine table, 1024 entries, +-127.
 * A power of two, so the top bits of the phase accumulator index it directly */
#define SINE_BITS       10
#define SINE_SIZE       (1 << SINE_BITS)
#define AMP_TABLES      (PCM_LEVEL_STEPS + 1)
#define AMP_SWEEP_TAB   PCM_LEVEL_STEPS

/* Loop buffer, frames: 121 ms at 44.1 kHz, 85% of a 10-frame block, so
 * a Sine step is one loop and a 20-frame Level or Separation step two. At
 * 22.05 kHz the same buffer lasts 243 ms, one loop for a 20-frame step */
#define PCMX_STEP_FRAMES  5355u
#define PCMX_CLOCK_FRAMES 4096u /* whole rate / 8 periods */

/* Results */
typedef struct {
    char        *name;
    unsigned    expected;
    unsigned    got;
} pcm_count_t;

/* ---- PCM.C ---- */

extern int8_t pcm_sine[SINE_SIZE];
extern int8_t pcm_amp_sine[AMP_TABLES][SINE_SIZE];

/* DMA buffers, from pcm_buf_alloc() */
extern uint8_t __far *pcm_clock_buf;
extern uint8_t __far *pcm_step_buf[2];
extern uint16_t       pcm_step_len[2];

extern int       pcm_ready;
extern uint8_t   pcm_dsp_major, pcm_dsp_minor;
extern int       pcm_hs_ok;
extern double    pcm_frame_rate;
extern uint16_t  pcm_clock_len;
extern uint8_t   pcm_tc;
extern int       pcm_dsp_busy;

extern pcm_count_t pcm_count_adpcm_clock;
extern pcm_count_t pcm_count_adpcm[2];

/* The clock playing, 8-bit or ADPCM, and the step waiting for its IRQ */
extern pcm_count_t *pcm_clock_count;
extern int       pcm_clock_playing, pcm_clock_pending;
extern unsigned  pcm_step_irq_base;
extern int       pcm_step_pending;

/* The 16-bit stereo driver, NULL when 16-bit doesn't play */
extern pcm16_driver_t *pcm16;

int  pcm_dsp_play(uint8_t cmd, uint16_t len);
void pcm_clock_settle();
void pcm_clock_stop();
int  pcm_print_count(pcm_count_t *count);
void __far *pcm_buf_place(uint32_t bytes);

/* ---- PCMX.C ---- */

extern uint8_t __far *pcmx_clock8_buf;
extern int16_t __far *pcmx_clock16_buf;
extern uint8_t __far *pcmx_step_buf[2];
extern pcm_count_t    pcmx_counts[PCMX_KIND_COUNT];
extern char           *pcm16_failure;

void pcmx_init(blaster_cfg_t *cfg);
void pcmx_fill_samples(unsigned max_frames);
void pcmx_poll();
void pcmx_finish();
int  pcmx_print_report();

/* ---- ADPCM.C ---- */

void pcm_adpcm_fill_samples(unsigned max_samples);

#endif
