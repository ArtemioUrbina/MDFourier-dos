#ifndef IRQ_H
#define IRQ_H

#include <i86.h>

/* Save FLAGS and CLI / restore FLAGS for short critical sections */
unsigned irq_save();
void     irq_restore(unsigned flags);

#pragma aux irq_save    = "pushf" "pop ax" "cli" value [ax] modify exact [ax];
#pragma aux irq_restore = "push ax" "popf" parm [ax] modify exact [];

#endif
