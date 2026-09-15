#ifndef LINUX_COMPAT_INTERRUPT_H
#define LINUX_COMPAT_INTERRUPT_H

#include "types.h"

typedef int irqreturn_t;
#define IRQ_NONE 0
#define IRQ_HANDLED 1
#define IRQ_WAKE_THREAD 2

#define IRQF_SHARED 0x00000080
#define IRQF_TRIGGER_NONE 0

typedef irqreturn_t (*lc_irq_handler_t)(int, void *);

int request_irq(unsigned int irq, lc_irq_handler_t handler, unsigned long flags, const char *name, void *dev_id);
void free_irq(unsigned int irq, void *dev_id);
void enable_irq(unsigned int irq);
void disable_irq(unsigned int irq);

#endif
