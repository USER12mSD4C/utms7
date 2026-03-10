#include "kapi.h"
#include "memory.h"
#include "idt.h"

extern u32 system_ticks;

u32 kapi_memory_used(void) {
    return memory_used();
}

u32 kapi_memory_free(void) {
    return memory_free();
}

u32 kapi_get_ticks(void) {
    return system_ticks;
}

void kapi_yield(void) {
    __asm__ volatile ("hlt");
}
