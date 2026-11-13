/*
 * 8259 interrupt controllers: the IRQ lines, their vectors and masks
 * Intel 8259A data sheet
 */

#include <conio.h>
#include <dos.h>
#include <i86.h>
#include "pcmdrv.h"

/* IRQ 0-7 are vectors 0x08-0x0F, IRQ 8-15 (second PIC) 0x70-0x77 */
unsigned pic_vector_of(uint8_t line) {
    return line < 8 ? 0x08 + line : 0x70 + (line - 8);
}

/* To the second PIC as well for IRQ 8-15. It reaches the CPU through
 * the first one (cascade on IRQ 2) */
void pic_eoi(uint8_t line) {
    if (line >= 8)
        outp(PIC2_CMD, PIC_EOI);
    outp(PIC1_CMD, PIC_EOI);
}

/* OCW3 0x0A: the next read of the command port gives the IRR */
uint16_t pic_irr() {
    uint16_t lo, hi;

    outp(PIC1_CMD, 0x0A);
    lo = (uint16_t)inp(PIC1_CMD);
    outp(PIC2_CMD, 0x0A);
    hi = (uint16_t)inp(PIC2_CMD);
    return (uint16_t)(lo | (hi << 8));
}

uint8_t irq_line_of(uint16_t before, uint16_t during, uint16_t after) {
    uint16_t raised = (uint16_t)(during & ~before & ~after & IRQ_CANDIDATES);
    uint8_t  line = 0;
    int      bit, found = 0;

    for (bit = 0; bit < 16; bit++)
        if (raised & (1u << bit)) {
            line = (uint8_t)bit;
            found++;
        }
    return found == 1 ? line : 0;
}

/* IRQ 8-15 also need IRQ 2 unmasked on the first PIC, the second one
 * goes through it */
void irq_hook_install(irq_hook_t *hook, uint8_t line, void (__interrupt __far *isr)()) {
    _disable();

    hook->line       = line;
    hook->old_vector = _dos_getvect(pic_vector_of(line));
    _dos_setvect(pic_vector_of(line), isr);
    hook->old_pic1 = (uint8_t)inp(PIC1_DATA);
    if (line >= 8) {
        hook->old_pic2 = (uint8_t)inp(PIC2_DATA);
        outp(PIC2_DATA, hook->old_pic2 & ~(1 << (line - 8)));
        outp(PIC1_DATA, hook->old_pic1 & ~(1 << 2));        /* cascade */
    } else
        outp(PIC1_DATA, hook->old_pic1 & ~(1 << line));

    _enable();
    hook->installed = 1;
}

void irq_hook_remove(irq_hook_t *hook) {
    if (!hook->installed)
        return;

    _disable();
    if (hook->line >= 8)
        outp(PIC2_DATA, hook->old_pic2);
    outp(PIC1_DATA, hook->old_pic1);
    _dos_setvect(pic_vector_of(hook->line), hook->old_vector);
    _enable();

    hook->installed = 0;
}
