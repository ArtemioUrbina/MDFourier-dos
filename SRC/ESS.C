/*
 * ESS ES18xx Extended mode, 16-bit stereo
 * Compatibility mode plays 16-bit stereo only to 11 kHz, Extended mode to
 * 48 kHz on the same 8-bit DMA. Mono and 8-bit stereo stay compatible,
 * the data sheet says not to mix: Extended from the 16-bit section's
 * reset to pcm_finish(). The counter reloads each loop, one IRQ each,
 * ess_poll() turns auto-init off after the next to last one.
 * https://www.philscomputerlab.com/uploads/3/7/2/3/37231621/es1869techmanual.pdf
 */

#include <stdio.h>
#include <conio.h>
#include "pcmdrv.h"
#include "dsp.h"
#include "pit.h"

#define ESS_RESET_PORT  6
#define ESS_RESET_FIFO  3       /* to the reset port: reset and clear the FIFO */
#define ESS_EXT_ON      0xC6    /* enables the Extended mode commands */
#define ESS_READ_REG    0xC0
#define ESS_RATE        0xA1    /* bit 7: 795.5 kHz / (256 - x) */
#define ESS_FILTER      0xA2    /* 7.16 MHz / (256 - x), roll-off at 1/82 */
#define ESS_COUNT_LO    0xA4    /* bytes per IRQ, two's complement */
#define ESS_COUNT_HI    0xA5
#define ESS_ANALOG      0xA8    /* bits 1-0: 01 stereo, 10 mono */
#define ESS_IRQ_CTRL    0xB1
#define ESS_DRQ_CTRL    0xB2
#define ESS_DAC_HOLD    0xB6    /* 0x00 sets up the DAC for signed data */
#define ESS_CTRL1       0xB7    /* FIFO format */
#define ESS_CTRL2       0xB8    /* bit 2 auto-initialize, bit 0 run */
/* Transfer type: the data sheet's table and steps disagree, 01 works
 * either way */
#define ESS_XFER_TYPE   0xB9
#define ESS_SINGLE      0x01

/* 44194 Hz by the data sheet, 44110.8 Hz measured */
#define ESS_RATE_DIV    238
/* Roll-off ~17.5 kHz */
#define ESS_FILTER_DIV  251
#define ESS_IRQ_ON      0x50    /* bit 6 counter IRQ, bit 4 set, data sheet step 6 */
#define ESS_DRQ_ON      0x50    /* bit 6 Extended mode DRQ, bit 4 set */
/* Table 16, 16-bit signed stereo: 71h, then BCh */
#define ESS_FMT_INIT    0x71
#define ESS_FMT_S16ST   0xBC
#define ESS_RUN         0x01
#define ESS_AUTO        0x04

int      ess_ext_mode;          /* in Extended mode now */
unsigned ess_loops;             /* loops of the tone playing */
int      ess_autoinit_on;       /* auto-initialize still on */

int ess_write(uint8_t reg, uint8_t val) {
    return dsp_write(sb_base, reg) && dsp_write(sb_base, val);
}

int ess_read(uint8_t reg) {
    if (!dsp_write(sb_base, ESS_READ_REG) || !dsp_write(sb_base, reg))
        return -1;

    return dsp_read(sb_base);
}

/* As dsp_reset() with the FIFO cleared too */
int ess_reset() {
    outp(sb_base + ESS_RESET_PORT, ESS_RESET_FIFO);
    pit_delay_us(5);
    outp(sb_base + ESS_RESET_PORT, 0);
    return dsp_read(sb_base) == 0xAA;
}

/* Reset into Extended mode, 16-bit signed stereo at 44111 Hz */
int ess_setup() {
    int analog;

    ess_ext_mode = 0;
    if (!ess_reset() || !dsp_write(sb_base, ESS_EXT_ON))
        return 0;

    analog = ess_read(ESS_ANALOG);
    if (analog < 0)
        return 0;

    ess_write(ESS_CTRL2, 0);                    /* DAC direction, stopped */
    ess_write(ESS_ANALOG, (uint8_t)((analog & ~3) | 1));
    ess_write(ESS_XFER_TYPE, ESS_SINGLE);
    ess_write(ESS_RATE, ESS_RATE_DIV);
    ess_write(ESS_FILTER, ESS_FILTER_DIV);
    ess_write(ESS_DAC_HOLD, 0x00);
    ess_write(ESS_CTRL1, ESS_FMT_INIT);
    ess_write(ESS_CTRL1, ESS_FMT_S16ST);
    ess_write(ESS_IRQ_CTRL, ESS_IRQ_ON);
    ess_write(ESS_DRQ_CTRL, ESS_DRQ_ON);
    /* The reset took the DAC off the mixer, the gap gives it the ~100 ms
     * the data sheet asks */
    dsp_write(sb_base, DSP_SPEAKER_ON);

    if (ess_read(ESS_RATE) != ESS_RATE_DIV)
        return 0;

    ess_ext_mode = 1;
    return 1;
}

void ess_print_info() {
    printf(" PCM 16-bit: stereo, 44111 Hz, ESS Extended mode, DMA %u\n", sb_dma);
}

/* Into Extended mode, a card that doesn't take it plays fillers */
char *ess_begin() {
    if (ess_setup())
        return NULL;

    pcm_recover();
    return "ESS Extended mode not accepted, 16-bit not played\n";
}

/* One IRQ per loop */
unsigned ess_play(void __far *buf, unsigned frames, unsigned loops) {
    uint16_t bytes  = (uint16_t)(4 * frames);   /* under 21.5 KB */
    uint16_t reload = (uint16_t)(0 - bytes);

    if (!ess_ext_mode)
        return 0;

    ess_write(ESS_CTRL2, 0);
    dma_program(sb_dma, buf, bytes, DMA_MODE_AUTO);
    ess_write(ESS_COUNT_LO, (uint8_t)(reload & 0xFF));
    ess_write(ESS_COUNT_HI, (uint8_t)(reload >> 8));

    ess_loops       = loops;
    ess_autoinit_on = loops > 1;
    if (!ess_write(ESS_CTRL2, (uint8_t)(ess_autoinit_on ? ESS_AUTO | ESS_RUN : ESS_RUN)))
        return 0;

    return loops;
}

void ess_poll(unsigned irqs) {
    if (ess_autoinit_on && irqs >= ess_loops - 1) {
        ess_write(ESS_CTRL2, ESS_RUN);
        ess_autoinit_on = 0;
    }
}

void ess_after_reset() {
    if (ess_ext_mode)
        ess_setup();
}

/* Back to compatibility mode, nothing left running */
void ess_end() {
    if (!ess_ext_mode)
        return;

    ess_write(ESS_CTRL2, 0);
    ess_autoinit_on = 0;
    ess_ext_mode    = 0;
    ess_reset();
}

pcm16_driver_t ess_driver = {
    " (ESS Extended mode)",
    ess_print_info,
    ess_begin,
    ess_play,
    ess_poll,
    NULL,               /* stop: the 8-bit channel, PCM.C masks it */
    ess_after_reset,
    ess_end,
    NULL                /* shared_isr: acked as any DSP IRQ */
};
