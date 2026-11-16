#ifndef PCMINT_H
#define PCMINT_H

/* Shared by PCM.C, PCMX.C and ADPCM.C */

#include <stdint.h>
#include "pcm.h"
#include "pcmdrv.h"

#define DSP_READ_READY  0x0E    /* reading it acks the 8-bit IRQ */
#define DSP_ACK_16      0x0F    /* reading it acks the 16-bit IRQ, DSP 4.xx */
#define SB16_IRQ_STATUS 0x82    /* mixer: bit 0 8-bit, bit 1 16-bit */

#define DSP_PLAY_8BIT   0x14    /* single-cycle, 8-bit mono, every DSP */
#define DSP_HS_8BIT     0x91    /* high-speed 8-bit single-cycle, DSP 2.01 and up */
#define DSP_BLOCK_SIZE  0x48    /* its length, sent before it */

/* Rate = 1000000 / (256 - TC), 211 is 22222.2 Hz, normal mode on every
 * DSP */
#define PCM_TC          211
#define PCM_RATE        (1000000.0 / 45.0)

/* High-speed: 233 is 43478.3 Hz, what games used for 44 kHz mono on
 * SB 2.0 and Pro. The DSP takes no commands while it plays, every DSP
 * from 2.01 the same commands. SB Pro filter on, as for all 8-bit mono */
#define PCM_HS_TC       233
#define PCM_HS_RATE     (1000000.0 / 23.0)
#define HS_SWEEP_HIGH   20000.0

/* Share of the block a tone fills, the rest silence: the slowest card
 * measured plays 2.2% long */
#define FILL_PCT        85

/* Clock tone, rate / 8 (2777.8 Hz), below any output filter. MDFourier
 * reads the card's crystal from it. Each period ends on 80h so the DAC
 * rests at the midpoint. DMA loops it, the DSP plays one transfer */
#define CLK_PERIOD      8
#define CLK_MAX         5600
#define CLK_PCT         95

/* Steps. Level at rate / 22, the same 22 samples every period */
#define SWEEP_LOW       20.0
#define SWEEP_HIGH      10000.0
#define LEVEL_FREQ      (PCM_RATE / 22.0)   /* 1010.1 Hz */
#define FADE            32                  /* samples, in and out */

/* Sweep and clock 3 dB down so the first Level step is the loudest PCM
 * element, MDFourier normalizes to it */
#define SWEEP_AMP       181                 /* of 256, -3.0 dB */
#define CLK_AMP         90.0                /* of 127, -3.0 dB */
#define STEP_MAX        5600                /* 20 frames at 85% */

/* Samples built per frame, a few ms even without an FPU */
#define FILL_CHUNK      768

/* ADPCM samples encoded per frame */
#define ADPCM_CHUNK     256

/* Sine table, 1024 entries, +-127, indexed by the phase's top bits */
#define SINE_BITS       10
#define SINE_SIZE       (1 << SINE_BITS)
#define AMP_TABLES      (PCM_LEVEL_STEPS + 1)
#define AMP_SWEEP_TAB   PCM_LEVEL_STEPS

/* Loop buffer: 121 ms at 44.1 kHz, 85% of a 10-frame block */
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

/* The clock playing and the step waiting for its IRQ */
extern pcm_count_t *pcm_clock_count;
extern int       pcm_clock_playing, pcm_clock_pending;
extern unsigned  pcm_step_irq_base;
extern int       pcm_step_pending;

/* NULL when 16-bit doesn't play */
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
