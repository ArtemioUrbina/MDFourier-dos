/*
 * WSS codec, 16-bit stereo on SB Pro compatibles with one (OPL3-SAx)
 *
 * The Yamaha OPL3-SAx has a DSP 3.01 for Sound Blaster Pro and next to
 * it a Windows Sound System codec (AD1848/CS4231 compatible), which is
 * what Windows plays 16-bit through. Pots are usually 530h. SAX.C
 * finds it and its settings, this plays through it.
 *
 * Registers are indexed: the index at base+4, the data at base+5.
 *
 * The playback count is in samples (stereo frames) and the codec
 * doesn't stop at zero: it raises the IRQ, reloads and goes on. A tone
 * is one count of all its loops, the 8237 looping the buffer under it,
 * and the IRQ handler clears Playback Enable as it comes, so it stops
 * on the end of the last loop.
 *
 * Source: Yamaha YMF715E (OPL3-SA3) data sheet
 * https://theretroweb.com/chips/6084
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <i86.h>
#include "wss.h"
#include "pcmdrv.h"
#include "env.h"
#include "pit.h"

#define WSS_ID          3       /* bits 5-0 read 04h */
#define WSS_INDEX       4       /* bit 7 INIT (codec busy), bit 6 MCE */
#define WSS_DATA        5
#define WSS_STATUS      6       /* bit 0 INT, any write clears it */
#define WSS_INIT        0x80
#define WSS_MCE         0x40
#define WSS_STATUS_INT  0x01

#define WSS_DAC_L       0x06    /* bit 7 mute, bits 5-0 attenuation, 1.5 dB */
#define WSS_DAC_R       0x07
#define WSS_FORMAT      0x08
#define WSS_IFACE       0x09    /* bit 3 ACAL, bit 2 single DMA, bit 0 PEN */
#define WSS_PIN         0x0A    /* bit 1 IRQ enable */
#define WSS_COUNT_HI    0x0E
#define WSS_COUNT_LO    0x0F

/* 16-bit signed little endian (01), stereo, divider 5 on the 16.9344 MHz clock: 44100 Hz */
#define WSS_FMT_S16ST   0x5B
/* Single DMA channel, no autocalibration: it takes long enough to make the next frame late.
 * The codec was calibrated at power up */
#define WSS_IFACE_SDC   0x04
#define WSS_PEN         0x01
#define WSS_IEN         0x02

/* Waits on the codec busy, 20 ms at most */
#define WSS_TIMEOUT     PIT_US_TO_CLK(20000UL)

uint16_t wss_base;
uint8_t  wss_irq, wss_dma;
int      wss_used;              /* wss_use() called: 16-bit plays here */
int      wss_ready;             /* set up for 16-bit, until wss_end() */
uint8_t  wss_iface_idle;        /* interface register, PEN off */
irq_hook_t wss_hook;            /* when the codec has its own IRQ line */

struct {
    uint8_t format, iface, dac_l, dac_r, pin;
} wss_saved;

char     wss_failure[128];

int wss_wait() {
    uint32_t start = pit_now();

    while (inp(wss_base + WSS_INDEX) & WSS_INIT)
        if ((uint32_t)(pit_now() - start) > WSS_TIMEOUT)
            return 0;

    return 1;
}

void wss_out(uint8_t reg, uint8_t val, uint8_t mce) {
    wss_wait();
    outp(wss_base + WSS_INDEX, mce | reg);
    outp(wss_base + WSS_DATA, val);
}

uint8_t wss_in(uint8_t reg) {
    wss_wait();
    outp(wss_base + WSS_INDEX, reg);
    return (uint8_t)inp(wss_base + WSS_DATA);
}

/* The index without MCE ends a mode change, the codec busy meanwhile */
int wss_mce_off() {
    wss_wait();
    outp(wss_base + WSS_INDEX, WSS_IFACE);
    return wss_wait();
}

int wss_present(unsigned port) {
    return (inp(port + WSS_ID) & 0x3F) == 0x04 && inp(port + WSS_INDEX) != 0xFF;
}

void wss_select(unsigned port) {
    wss_base = (uint16_t)port;
}

void wss_use(unsigned port, uint8_t irq, uint8_t dma) {
    wss_base = (uint16_t)port;
    wss_irq  = irq;
    wss_dma  = dma;
    wss_used = 1;
}

int wss_in_use() {
    return wss_used;
}

/* ========= IRQ ========= */

void wss_service() {
    if (inp(wss_base + WSS_STATUS) & WSS_STATUS_INT) {
        outp(wss_base + WSS_STATUS, 0);
        outp(wss_base + WSS_INDEX, WSS_IFACE);
        outp(wss_base + WSS_DATA, wss_iface_idle);
        irq_count++;
    }
}

/* SB IRQ line, in the 16-bit section only the codec plays */
int wss_shared_isr() {
    if (!wss_ready || wss_irq != sb_irq)
        return 0;

    wss_service();
    return 1;
}

void __interrupt __far wss_isr() {
    wss_service();
    pic_eoi(wss_irq);
}

void wss_unhook() {
    irq_hook_remove(&wss_hook);
}

#define PROBE_FRAMES    16

/* 64 bytes that don't cross a 64 KB page: one of the two halves */
uint8_t __far probe_area[128];

void __far *probe_buffer() {
    uint32_t phys = ((uint32_t)FP_SEG(probe_area) << 4) + FP_OFF(probe_area);

    if ((phys >> 16) != ((phys + 63) >> 16))
        return probe_area + 64;

    return probe_area;
}

/* The IRQ line the codec raises. Its configuration register leaves the
 * IRQ out on some boards (the OPL3-SA2 reads 03h: DMA 3, no IRQ), so
 * play 16 silent frames with interrupts off and see which request line
 * goes up while the codec holds it, and down once acked. The line is
 * edge triggered: the request stays in the IRR while it is high, masked
 * or not. 
 * Playing through the codec moves the OPL3-SAx out of its SB mode (see
 * SAX.C): a run probes only right before its 16-bit section */
uint8_t wss_probe_irq(uint8_t dma) {
    void __far *buffer = probe_buffer();
    uint8_t  pin, iface;
    uint16_t before, during, after;
    uint32_t start;

    if (!wss_wait())
        return 0;

    _fmemset(buffer, 0, PROBE_FRAMES * 4);
    pin   = wss_in(WSS_PIN);
    iface = (uint8_t)(wss_in(WSS_IFACE) & ~WSS_PEN);

    _disable();
    wss_out(WSS_FORMAT, WSS_FMT_S16ST, WSS_MCE);
    wss_out(WSS_IFACE, WSS_IFACE_SDC, WSS_MCE);
    wss_mce_off();
    dma_program(dma, buffer, PROBE_FRAMES * 4, DMA_MODE_AUTO);
    wss_out(WSS_COUNT_LO, PROBE_FRAMES - 1, 0);
    wss_out(WSS_COUNT_HI, 0, 0);
    wss_out(WSS_PIN, (uint8_t)(pin | WSS_IEN), 0);
    outp(wss_base + WSS_STATUS, 0);

    before = pic_irr();
    wss_out(WSS_IFACE, WSS_IFACE_SDC | WSS_PEN, 0);

    /* 16 frames are 0.4 ms, wait up to 20 */
    start = pit_now();
    while (!(inp(wss_base + WSS_STATUS) & WSS_STATUS_INT))
        if ((uint32_t)(pit_now() - start) > WSS_TIMEOUT)
            break;
    during = pic_irr();

    wss_out(WSS_IFACE, WSS_IFACE_SDC, 0);
    outp(wss_base + WSS_STATUS, 0);
    after = pic_irr();
    dma_mask(dma);

    wss_out(WSS_PIN, pin, 0);
    wss_out(WSS_IFACE, iface, WSS_MCE);
    wss_mce_off();
    _enable();

    return irq_line_of(before, during, after);
}

/* ---- Playback ---- */

void wss_print_info() {
    printf(" PCM 16-bit: stereo, 44100 Hz, WSS at 0x%03X, ", wss_base);
    if (wss_irq)
        printf("IRQ %u", wss_irq);
    else
        printf("IRQ probed before 16-bit");
    printf(", DMA %u%s\n", wss_dma, wss_cfg.mode == WSS_MANUAL ? " (/W)" : "");
}

/* 16-bit signed stereo at 44100 Hz, DAC at 0 dB, IRQ on. 1 if the format reads back */
int wss_setup() {
    wss_ready = 0;
    if (!wss_wait())
        return 0;

    wss_saved.format = wss_in(WSS_FORMAT);
    wss_saved.iface  = wss_in(WSS_IFACE);
    wss_saved.dac_l  = wss_in(WSS_DAC_L);
    wss_saved.dac_r  = wss_in(WSS_DAC_R);
    wss_saved.pin    = wss_in(WSS_PIN);

    wss_iface_idle = WSS_IFACE_SDC;
    wss_out(WSS_FORMAT, WSS_FMT_S16ST, WSS_MCE);
    wss_out(WSS_IFACE, wss_iface_idle, WSS_MCE);
    if (!wss_mce_off())
        return 0;

    wss_out(WSS_DAC_L, 0x00, 0);
    wss_out(WSS_DAC_R, 0x00, 0);
    wss_out(WSS_PIN, (uint8_t)(wss_saved.pin | WSS_IEN), 0);
    outp(wss_base + WSS_STATUS, 0);

    if (wss_in(WSS_FORMAT) != WSS_FMT_S16ST)
        return 0;

    wss_ready = 1;
    return 1;
}

/* In the gap before the 16-bit section: the IRQ probed if /W:AUTO didn't find it, its handler in, the codec set up */
char *wss_begin() {
    if (!wss_irq) {
        wss_irq = wss_probe_irq(wss_dma);
        if (!wss_irq) {
            sprintf(wss_failure, "WSS codec at 0x%03X, its IRQ didn't show up, 16-bit not played.\n"
                    "           Give it with /W:%X,irq,%u\n", wss_base, wss_base, wss_dma);
            return wss_failure;
        }
    }

    if (wss_irq != sb_irq && !wss_hook.installed) {
        irq_hook_install(&wss_hook, wss_irq, wss_isr);
        atexit(wss_unhook);
    }

    if (!wss_setup()) {
        sprintf(wss_failure, "WSS codec at 0x%03X not answering, 16-bit not played\n", wss_base);
        return wss_failure; 
    }

    return NULL;
}

/* A whole tone: one count of frames * loops (32768 at most), the 8237 looping the buffer. One IRQ, at its end */
unsigned wss_play(void __far *buf, unsigned frames, unsigned loops) {
    uint16_t count = (uint16_t)((uint32_t)frames * loops - 1);

    if (!wss_ready)
        return 0;

    wss_out(WSS_IFACE, wss_iface_idle, 0);
    dma_program(wss_dma, buf, (uint16_t)(4 * frames), DMA_MODE_AUTO);
    wss_out(WSS_COUNT_LO, (uint8_t)(count & 0xFF), 0);
    wss_out(WSS_COUNT_HI, (uint8_t)(count >> 8), 0);
    outp(wss_base + WSS_STATUS, 0);

    wss_out(WSS_IFACE, (uint8_t)(wss_iface_idle | WSS_PEN), 0);
    return 1;
}

void wss_stop() {
    if (!wss_ready)
        return;

    wss_out(WSS_IFACE, wss_iface_idle, 0);
    dma_mask(wss_dma);
}

void wss_end() {
    if (!wss_ready)
        return;

    wss_stop();
    wss_out(WSS_PIN, wss_saved.pin, 0);
    wss_out(WSS_DAC_L, wss_saved.dac_l, 0);
    wss_out(WSS_DAC_R, wss_saved.dac_r, 0);
    wss_out(WSS_FORMAT, wss_saved.format, WSS_MCE);
    wss_out(WSS_IFACE, (uint8_t)(wss_saved.iface & ~WSS_PEN), WSS_MCE);
    wss_mce_off();
    outp(wss_base + WSS_STATUS, 0);
    wss_ready = 0;
}

pcm16_driver_t wss_driver = {
    " (WSS codec)",
    wss_print_info,
    wss_begin,
    wss_play,
    NULL,               /* poll */
    wss_stop,
    NULL,               /* after_reset */
    wss_end,
    wss_shared_isr
};
