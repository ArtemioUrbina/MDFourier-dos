/*
 * 8237 DMA controllers: channels 0-3 move bytes, 5-7 words
 * Intel 8237A data sheet
 */

#include <conio.h>
#include <i86.h>
#include "pcmdrv.h"

/* 8237 page registers (address bits 16-23), in this odd order since
 * the original PC */
uint8_t dma_page_port[4] = { 0x87, 0x83, 0x81, 0x82 };

/* Second 8237, channels 5-7, in the odd order of the first */
uint8_t dma16_page_port[4] = { 0x8F, 0x8B, 0x89, 0x8A };

/* Real mode address: segment * 16 + offset. DMA needs the physical one */
uint32_t dma_phys_of(void __far *ptr) {
    return ((uint32_t)FP_SEG(ptr) << 4) + FP_OFF(ptr);
}

/* Channels 0-3. Masked while programming so it can't start halfway.
 * The flip-flop picks which byte of a 16-bit register the next write
 * goes to.
 * Cleared, low byte first.
 * The count register takes length - 1, it transfers until it counts past 0 */
void dma_program(uint8_t channel, void __far *buf, uint16_t len, uint8_t mode) {
    uint32_t phys        = dma_phys_of(buf);
    uint16_t page_offset = (uint16_t)(phys & 0xFFFF);
    uint16_t len_minus_1 = (uint16_t)(len - 1);

    outp(DMA_MASK, 0x04 | channel);
    outp(DMA_FLIPFLOP, 0);
    outp(DMA_MODE, mode | channel);
    outp(channel * 2, page_offset & 0xFF);
    outp(channel * 2, page_offset >> 8);
    outp(channel * 2 + 1, len_minus_1 & 0xFF);
    outp(channel * 2 + 1, len_minus_1 >> 8);
    outp(dma_page_port[channel], (uint8_t)(phys >> 16));
    outp(DMA_MASK, channel);
}

void dma_mask(uint8_t channel) {
    outp(DMA_MASK, 0x04 | channel);
}

/* Bytes left to transfer, minus 1: 0xFFFF once all went */
uint16_t dma_count_left(uint8_t channel) {
    uint16_t lo, hi;

    outp(DMA_FLIPFLOP, 0);
    lo = (uint16_t)inp(channel * 2 + 1);
    hi = (uint16_t)inp(channel * 2 + 1);
    return (uint16_t)(lo | (hi << 8));
}

/* Channels 5-7, the same steps as dma_program() on the second 8237 */
void dma16_program(uint8_t high_channel, void __far *buf, uint16_t words, uint8_t mode) {
    uint32_t phys          = dma_phys_of(buf);
    uint8_t  channel       = (uint8_t)(high_channel & 3);
    uint16_t word_offset   = (uint16_t)((phys >> 1) & 0xFFFF);
    uint16_t words_minus_1 = (uint16_t)(words - 1);

    outp(DMA16_MASK, 0x04 | channel);
    outp(DMA16_FLIPFLOP, 0);
    outp(DMA16_MODE, mode | channel);
    outp(0xC0 + channel * 4, word_offset & 0xFF);
    outp(0xC0 + channel * 4, word_offset >> 8);
    outp(0xC2 + channel * 4, words_minus_1 & 0xFF);
    outp(0xC2 + channel * 4, words_minus_1 >> 8);
    outp(dma16_page_port[channel], (uint8_t)((phys >> 16) & 0xFE));
    outp(DMA16_MASK, channel);
}

