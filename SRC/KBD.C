#include <stdint.h>
#include <i86.h>
#include "kbd.h"
#include "irq.h"

/* BIOS data area, segment 0040h */
#define BDA_KBD_HEAD    0x1A
#define BDA_KBD_TAIL    0x1C
#define BDA_KBD_START   0x80
#define BDA_KBD_END     0x82

#define BDA_WORD(off)   (*(volatile uint16_t __far *)MK_FP(0x40, (off)))

#define KEY_ESC_ASCII   0x1B

int kbd_poll_escape(void)
{
    unsigned flags;
    uint16_t head, tail, start, end;
    int esc = 0;

    flags = irq_save();

    head  = BDA_WORD(BDA_KBD_HEAD);
    tail  = BDA_WORD(BDA_KBD_TAIL);
    start = BDA_WORD(BDA_KBD_START);
    end   = BDA_WORD(BDA_KBD_END);
    if (end <= start) {             /* old BIOS: fixed buffer */
        start = 0x1E;
        end   = 0x3E;
    }

    /* ASCII in the low byte */
    while (head != tail) {
        if ((BDA_WORD(head) & 0xFF) == KEY_ESC_ASCII)
            esc = 1;
        head += 2;
        if (head >= end)
            head = start;
    }
    BDA_WORD(BDA_KBD_HEAD) = tail;

    irq_restore(flags);

    return esc;
}
