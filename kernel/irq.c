#include "idt.h"
#include "sched.h"
#include "../include/io.h"

extern irq_handler_t irq_handlers[16];

static void send_eoi(int irq) {
    if (irq >= 8) outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

void irq_handler_dispatch(int irq) {
    if (irq >= 0 && irq < 16) {
        sched_wake_irq(irq);
        if (irq_handlers[irq]) {
            irq_handlers[irq]();
        }
        send_eoi(irq);
    }
}
