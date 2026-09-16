#include "idt.h"
#include "../../core/sched.h"
#include "../../../include/io.h"

extern irq_handler_t irq_handlers[16];

void irq_handler_dispatch(int irq) {
    if (irq >= 0 && irq < 16) {
        sched_wake_irq(irq);
        if (irq_handlers[irq]) {
            irq_handlers[irq]();
        }
    }
}
