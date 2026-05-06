// kernel/memory.c
#include "memory.h"
#include "../include/string.h"

#define ALIGN_UP(x, align) (((x) + (align) - 1) & ~((align) - 1))

static block_header_t* free_list = NULL;
static int initialized = 0;
static u64 total_memory = 0;
static u64 used_memory = 0;

static void add_free_block(u64 base, u64 size) {
    if (size < sizeof(block_header_t) + 16) return;

    u64 aligned_base = ALIGN_UP(base, 8);
    u64 adjusted_size = size - (aligned_base - base);
    if (adjusted_size < sizeof(block_header_t) + 16) return;

    block_header_t* block = (block_header_t*)aligned_base;
    block->size = adjusted_size - sizeof(block_header_t);
    block->free = 1;
    block->next = free_list;
    free_list = block;
}

static void sort_free_list(void) {
    if (!free_list || !free_list->next) return;
    int swapped;
    do {
        swapped = 0;
        block_header_t** pp = &free_list;
        while (*pp && (*pp)->next) {
            block_header_t* a = *pp;
            block_header_t* b = a->next;
            if ((u64)a > (u64)b) {
                a->next = b->next;
                b->next = a;
                *pp = b;
                swapped = 1;
            }
            pp = &(*pp)->next;
        }
    } while (swapped);
}

static void merge_adjacent_blocks(void) {
    if (!free_list) return;
    sort_free_list();

    block_header_t* curr = free_list;
    while (curr && curr->next) {
        u8* curr_end = (u8*)curr + sizeof(block_header_t) + curr->size;
        if (curr_end == (u8*)curr->next) {
            curr->size += sizeof(block_header_t) + curr->next->size;
            curr->next = curr->next->next;
        } else {
            curr = curr->next;
        }
    }
}

void memory_init(u64 mem_start, u64 mem_size) {
    if (initialized) return;

    free_list = NULL;
    total_memory = 0;
    used_memory = 0;

    if (mem_size > 0) {
        add_free_block(mem_start, mem_size);
        total_memory += mem_size;
    }

    merge_adjacent_blocks();
    initialized = 1;
}

void memory_add_region(u64 base, u64 size) {
    if (!initialized || size == 0) return;
    add_free_block(base, size);
    total_memory += size;
    merge_adjacent_blocks();
}

void* kmalloc(u64 size) {
    if (!initialized || size == 0) return NULL;

    size = ALIGN_UP(size, 8);

    block_header_t* prev = NULL;
    block_header_t* curr = free_list;

    while (curr) {
        if (curr->free && curr->size >= size) {
            u64 remaining = curr->size - size;

            if (remaining >= sizeof(block_header_t) + 16) {
                block_header_t* new_block = (block_header_t*)((u8*)curr + sizeof(block_header_t) + size);
                new_block->size = remaining - sizeof(block_header_t);
                new_block->free = 1;
                new_block->next = curr->next;
                curr->size = size;
                curr->next = new_block;
            }

            if (prev)
                prev->next = curr->next;
            else
                free_list = curr->next;

            curr->free = 0;
            curr->next = NULL;
            used_memory += curr->size + sizeof(block_header_t);

            return (void*)((u8*)curr + sizeof(block_header_t));
        }
        prev = curr;
        curr = curr->next;
    }

    return NULL;
}

void kfree(void* ptr) {
    if (!initialized || !ptr) return;

    block_header_t* block = (block_header_t*)((u8*)ptr - sizeof(block_header_t));
    if (block->free) return;

    block->free = 1;
    used_memory -= block->size + sizeof(block_header_t);
    block->next = free_list;
    free_list = block;

    merge_adjacent_blocks();
}

u64 memory_used(void) { return used_memory; }
u64 memory_free(void) { return total_memory - used_memory; }
