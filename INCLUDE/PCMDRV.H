#ifndef PCMDRV_H
#define PCMDRV_H

#include <stdint.h>

/* DSP commands the drivers share */
#define DSP_SPEAKER_ON  0xD1
#define DSP_SPEAKER_OFF 0xD3
#define DSP_TIME_CONST  0x40

/* The Sound Blaster, from BLASTER */
extern uint16_t sb_base;
extern uint8_t  sb_irq, sb_dma;

/* IRQs served, any driver */
extern volatile unsigned irq_count;

/* Stopped transfer. DSP reset, back to the 8-bit mono rate, speaker on */
void recover();

/* ---- 8237, channels 0-3 ---- */
#define DMA_MASK        0x0A
#define DMA_MODE        0x0B
#define DMA_FLIPFLOP    0x0C
#define DMA_MODE_SINGLE 0x48    /* single, increment, memory to card */
#define DMA_MODE_AUTO   0x58    /* same, auto-initialize */

/* len bytes, within one 64 KB page */
void dma_program(uint8_t channel, void __far *buf, uint16_t len, uint8_t mode);
void dma_mask(uint8_t channel);

/* ---- 8259 ---- */
#define PIC1_CMD        0x20
#define PIC1_DATA       0x21
#define PIC2_CMD        0xA0
#define PIC2_DATA       0xA1
#define PIC_EOI         0x20

/* A handler on an IRQ line, unmasked at the PIC. Put back as it was */
typedef struct {
    uint8_t   line;
    int       installed;
    void      (__interrupt __far *old_vector)();
    uint8_t   old_pic1, old_pic2;
} irq_hook_t;

void     irq_hook_install(irq_hook_t *hook, uint8_t line, void (__interrupt __far *isr)());
void     irq_hook_remove(irq_hook_t *hook);
void     pic_eoi(uint8_t line);
uint16_t pic_irr();

/* Lines a card can be on: 3-7, 9-12 and 15 */
#define IRQ_CANDIDATES  0x9EF8
/* IRR bits up while a device held its request and down once acked */
uint8_t  irq_line_of(uint16_t before, uint16_t during, uint16_t after);

/* 16-bit stereo drivers */
typedef struct {
    char        *suffix;
    void        (*print_info)();
    char        *(*begin)();
    unsigned    (*play)(void __far *buf, unsigned frames, unsigned loops);
    void        (*poll)(unsigned irqs);
    void        (*stop)(); 
    void        (*after_reset)();
    void        (*end)();
    int         (*shared_isr)();
} pcm16_driver_t;

extern pcm16_driver_t ess_driver;
extern pcm16_driver_t wss_driver;

#endif
