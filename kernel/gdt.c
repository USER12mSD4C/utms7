#include "gdt.h"
#include "../include/string.h"
#include "../include/io.h"

struct gdt_entry {
    u16 limit_low;
    u16 base_low;
    u8 base_middle;
    u8 access;
    u8 granularity;
    u8 base_high;
    u32 base_upper;
    u32 reserved;
} __attribute__((packed));

struct gdt_ptr {
    u16 limit;
    u64 base;
} __attribute__((packed));

struct tss {
    u32 reserved0;
    u64 rsp0;
    u64 rsp1;
    u64 rsp2;
    u64 reserved1;
    u64 ist[7];
    u64 reserved2;
    u16 reserved3;
    u16 iomap_base;
} __attribute__((packed));

// 6 обычных записей по 16 байт = 96 байт, плюс ещё 8 байт для старших 32 бит TSS
#define GDT_SIZE 7
static struct gdt_entry gdt[GDT_SIZE] __attribute__((aligned(16)));
static struct tss kernel_tss;
static struct gdt_ptr gp;

void tss_set_rsp0(u64 rsp) {
    kernel_tss.rsp0 = rsp;
}

static void gdt_set_gate(u32 num, u64 base, u64 limit, u8 access, u8 gran) {
    gdt[num].limit_low = limit & 0xFFFF;
    gdt[num].base_low = base & 0xFFFF;
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high = (base >> 24) & 0xFF;
    gdt[num].base_upper = (base >> 32) & 0xFFFFFFFF;
    gdt[num].access = access;
    gdt[num].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt[num].reserved = 0;
}

int gdt_init(void) {
    // 7 записей: null, kernel code, kernel data, user code, user data, TSS low, TSS high
    gp.limit = sizeof(gdt) - 1;
    gp.base = (u64)gdt;

    memset(gdt, 0, sizeof(gdt));
    memset(&kernel_tss, 0, sizeof(kernel_tss));
    kernel_tss.iomap_base = sizeof(kernel_tss);

    // null
    gdt_set_gate(0, 0, 0, 0, 0);
    // kernel code
    gdt_set_gate(1, 0, 0xFFFFF, 0x9A, 0xAF);
    // kernel data
    gdt_set_gate(2, 0, 0xFFFFF, 0x92, 0xAF);
    // user code
    gdt_set_gate(3, 0, 0xFFFFF, 0xFA, 0xAF);
    // user data
    gdt_set_gate(4, 0, 0xFFFFF, 0xF2, 0xAF);

    // TSS - занимает 2 GDT записи (5 и 6)
    u64 tss_base = (u64)&kernel_tss;
    u32 tss_limit = sizeof(kernel_tss) - 1;

    // Запись 5: младшие 64 бита TSS дескриптора
    gdt[5].limit_low = tss_limit & 0xFFFF;
    gdt[5].base_low = tss_base & 0xFFFF;
    gdt[5].base_middle = (tss_base >> 16) & 0xFF;
    gdt[5].access = 0x89;
    gdt[5].granularity = 0x00;
    gdt[5].base_high = (tss_base >> 24) & 0xFF;
    gdt[5].base_upper = (tss_base >> 32) & 0xFFFFFFFF;
    gdt[5].reserved = 0;

    // Запись 6: старшие 32 бита (обязательно 0 для TSS)
    gdt[6].limit_low = 0;
    gdt[6].base_low = 0;
    gdt[6].base_middle = 0;
    gdt[6].access = 0;
    gdt[6].granularity = 0;
    gdt[6].base_high = 0;
    gdt[6].base_upper = 0;
    gdt[6].reserved = 0;

    __asm__ volatile ("lgdt %0" : : "m"(gp));

    // Загружаем TSS
    __asm__ volatile ("mov $0x28, %%ax; ltr %%ax" : : : "ax");

    // Перезагружаем сегменты данных
    __asm__ volatile (
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        : : : "ax"
    );

    return 0;
}
