// файл: kernel/memory.c
#include "memory.h"
#include "../include/string.h"

#define MEMORY_POOL_SIZE (64 * 1024 * 1024)

#define ALIGN_UP(x, align) (((x) + (align) - 1) & ~((align) - 1))
#define ALIGN_DOWN(x, align) ((x) & ~((align) - 1))

static u8 __attribute__((aligned(4096))) memory_pool[MEMORY_POOL_SIZE];

static block_header_t* free_list = NULL;
static int initialized = 0;
static u64 total_memory = 0;
static u64 used_memory = 0;

static void add_free_block(u64 base, u64 size) {
    if (size < sizeof(block_header_t) + 16) {
        return;
    }

    u64 aligned_base = ALIGN_UP(base, 8);
    u64 adjusted_size = size - (aligned_base - base);
    if (adjusted_size < sizeof(block_header_t) + 16) {
        return;
    }

    block_header_t* block = (block_header_t*)aligned_base;
    block->size = adjusted_size - sizeof(block_header_t);
    block->free = 1;
    block->next = free_list;
    free_list = block;
}

static u64 get_region_overlap(u64 start1, u64 size1, u64 start2, u64 size2) {
    u64 end1 = start1 + size1;
    u64 end2 = start2 + size2;
    if (end1 <= start2 || end2 <= start1) {
        return 0;
    }
    u64 overlap_start = (start1 > start2) ? start1 : start2;
    u64 overlap_end = (end1 < end2) ? end1 : end2;
    return overlap_end - overlap_start;
}

static void merge_adjacent_blocks(void) {
    if (!free_list) {
        return;
    }

    block_header_t* prev = NULL;
    block_header_t* curr = free_list;

    while (curr && curr->next) {
        u8* curr_end = (u8*)curr + sizeof(block_header_t) + curr->size;
        if (curr_end == (u8*)curr->next) {
            curr->size += sizeof(block_header_t) + curr->next->size;
            curr->next = curr->next->next;
            continue;
        }
        prev = curr;
        curr = curr->next;
    }

    curr = free_list;
    prev = NULL;
    while (curr && curr->next) {
        u8* curr_end = (u8*)curr + sizeof(block_header_t) + curr->size;
        if (curr_end == (u8*)curr->next) {
            curr->size += sizeof(block_header_t) + curr->next->size;
            curr->next = curr->next->next;
            continue;
        }
        prev = curr;
        curr = curr->next;
    }
}

void memory_init(u64 mem_start, u64 mem_size) {
    if (initialized) {
        return;
    }

    free_list = NULL;
    total_memory = 0;
    used_memory = 0;

    u64 pool_start = (u64)memory_pool;
    u64 pool_end = pool_start + MEMORY_POOL_SIZE;

    add_free_block(pool_start, MEMORY_POOL_SIZE);
    total_memory += MEMORY_POOL_SIZE;

    if (mem_size > 0) {
        u64 ext_start = mem_start;
        u64 ext_end = mem_start + mem_size;

        if (ext_start >= pool_end) {
            add_free_block(ext_start, mem_size);
            total_memory += mem_size;
        } else if (ext_end <= pool_start) {
            add_free_block(ext_start, mem_size);
            total_memory += mem_size;
        } else {
            u64 before_start = ext_start;
            u64 before_size = pool_start - ext_start;
            if (before_size > 0) {
                add_free_block(before_start, before_size);
                total_memory += before_size;
            }

            u64 after_start = pool_end;
            u64 after_size = ext_end - pool_end;
            if (after_size > 0) {
                add_free_block(after_start, after_size);
                total_memory += after_size;
            }

            u64 overlap = get_region_overlap(pool_start, MEMORY_POOL_SIZE, ext_start, mem_size);
            if (overlap > 0) {
                total_memory -= overlap;
            }
        }
    }

    merge_adjacent_blocks();

    initialized = 1;
}

void memory_add_region(u64 base, u64 size) {
    if (!initialized || size == 0) {
        return;
    }

    u64 pool_start = (u64)memory_pool;
    u64 pool_end = pool_start + MEMORY_POOL_SIZE;
    u64 new_start = base;
    u64 new_end = base + size;

    if (new_start >= pool_end || new_end <= pool_start) {
        add_free_block(new_start, size);
        total_memory += size;
    } else {
        u64 before_start = new_start;
        u64 before_size = pool_start - new_start;
        if (before_size > 0) {
            add_free_block(before_start, before_size);
            total_memory += before_size;
        }

        u64 after_start = pool_end;
        u64 after_size = new_end - pool_end;
        if (after_size > 0) {
            add_free_block(after_start, after_size);
            total_memory += after_size;
        }

        u64 overlap = get_region_overlap(pool_start, MEMORY_POOL_SIZE, new_start, size);
        if (overlap > 0) {
            total_memory -= overlap;
        }
    }

    merge_adjacent_blocks();
}

void* kmalloc(u64 size) {
    if (!initialized || size == 0) {
        return NULL;
    }

    size = ALIGN_UP(size, 8);

    block_header_t* current = free_list;
    block_header_t* prev = NULL;

    while (current != NULL) {
        if (current->free && current->size >= size) {
            u64 remaining = current->size - size;
            if (remaining >= sizeof(block_header_t) + 16) {
                block_header_t* new_block = (block_header_t*)((u8*)current + sizeof(block_header_t) + size);
                new_block->size = remaining - sizeof(block_header_t);
                new_block->free = 1;
                new_block->next = current->next;
                current->next = new_block;
                current->size = size;
            }

            current->free = 0;
            used_memory += current->size + sizeof(block_header_t);
            return (void*)((u8*)current + sizeof(block_header_t));
        }

        prev = current;
        current = current->next;
    }

    return NULL;
}

void kfree(void* ptr) {
    if (!initialized || ptr == NULL) {
        return;
    }

    block_header_t* block = (block_header_t*)((u8*)ptr - sizeof(block_header_t));
    if (block->free) {
        return;
    }

    block->free = 1;
    used_memory -= block->size + sizeof(block_header_t);

    merge_adjacent_blocks();
}

u64 memory_used(void) {
    if (!initialized) {
        return 0;
    }
    return used_memory;
}

u64 memory_free(void) {
    if (!initialized) {
        return 0;
    }
    return total_memory - used_memory;
}
