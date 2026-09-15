#include "types.h"
#include "interrupt.h"
extern void idt_register_irq(int irq, void (*handler)(void));
#include "../kernel/memory.h"

struct irq_action {
    lc_irq_handler_t handler;
    void *dev_id;
    const char *name;
    struct irq_action *next;
};

static struct irq_action *lc_irq_table[16];

static void lc_irq_run(unsigned int line) {
    struct irq_action *a = lc_irq_table[line];
    while (a) {
        a->handler((int)line, a->dev_id);
        a = a->next;
    }
}

#define LC_TRAMP(n) static void lc_tramp_##n(void) { lc_irq_run(n); }
LC_TRAMP(0) LC_TRAMP(1) LC_TRAMP(2) LC_TRAMP(3)
LC_TRAMP(4) LC_TRAMP(5) LC_TRAMP(6) LC_TRAMP(7)
LC_TRAMP(8) LC_TRAMP(9) LC_TRAMP(10) LC_TRAMP(11)
LC_TRAMP(12) LC_TRAMP(13) LC_TRAMP(14) LC_TRAMP(15)

static void (*lc_trampolines[16])(void) = {
    lc_tramp_0, lc_tramp_1, lc_tramp_2, lc_tramp_3,
    lc_tramp_4, lc_tramp_5, lc_tramp_6, lc_tramp_7,
    lc_tramp_8, lc_tramp_9, lc_tramp_10, lc_tramp_11,
    lc_tramp_12, lc_tramp_13, lc_tramp_14, lc_tramp_15
};

int request_irq(unsigned int irq, lc_irq_handler_t handler, unsigned long flags, const char *name, void *dev_id) {
    (void)flags;
    if (irq > 15 || !handler) return -1;
    struct irq_action *a = kmalloc(sizeof(struct irq_action));
    if (!a) return -1;
    a->handler = handler;
    a->dev_id = dev_id;
    a->name = name;
    a->next = lc_irq_table[irq];
    lc_irq_table[irq] = a;
    if (a->next == NULL) idt_register_irq((int)irq, lc_trampolines[irq]);
    return 0;
}

void free_irq(unsigned int irq, void *dev_id) {
    if (irq > 15) return;
    struct irq_action **pp = &lc_irq_table[irq];
    while (*pp) {
        if ((*pp)->dev_id == dev_id) {
            struct irq_action *dead = *pp;
            *pp = dead->next;
            kfree(dead);
            return;
        }
        pp = &(*pp)->next;
    }
}

void enable_irq(unsigned int irq) { (void)irq; }
void disable_irq(unsigned int irq) { (void)irq; }
