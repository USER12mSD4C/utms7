#ifndef MEMORY_H
#define MEMORY_H

#include "../include/types.h"

typedef struct block_header {
    u64 size;
    struct block_header* next;
    u8 free;
} block_header_t;

void memory_init(u64 mem_start, u64 mem_size); // заглушка
void memory_add_region(u64 base, u64 size);    // новая функция
void* kmalloc(u64 size);
void kfree(void* ptr);
u64 memory_used(void);
u64 memory_free(void);

#endif
