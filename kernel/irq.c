#include "idt.h"
#include "sched.h"

extern irq_handler_t irq_handlers[16];

void irq_handler_dispatch(int irq) {
    if (irq >= 0 && irq < 16 && irq_handlers[irq]) {
        irq_handlers[irq]();
    }

    // Если нужно переключение, вызываем планировщик
    if (sched_need_resched) {
        sched_switch();
    }
}
