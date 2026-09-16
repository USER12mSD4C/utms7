#include "memory.h"
#include "../include/string.h"

#define ALIGN_UP(x, align) (((x) + (align) - 1) & ~((align) - 1))

static inline u64 mem_lock(void) {
    u64 flags;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(flags) :: "memory");
    return flags;
}

static inline void mem_unlock(u64 flags) {
    __asm__ volatile("push %0; popfq" :: "r"(flags) : "memory");
}

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

void memory_init(u64 mem_start, u64 mem_size) {
    if (initialized) return;

    free_list = NULL;
    total_memory = 0;
    used_memory = 0;

    if (mem_size > 0) {
        add_free_block(mem_start, mem_size);
        total_memory += mem_size;
    }

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
    initialized = 1;
}

void memory_add_region(u64 base, u64 size) {
    if (!initialized || size == 0) return;
    add_free_block(base, size);
    total_memory += size;
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

void* kmalloc(u64 size) {
    if (!initialized || size == 0) return NULL;
    u64 flags = mem_lock();
    size = ALIGN_UP(size, 8);
    block_header_t* prev = NULL;
    block_header_t* curr = free_list;
    void* result = NULL;
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
            if (prev) prev->next = curr->next;
            else free_list = curr->next;
            curr->free = 0;
            curr->next = NULL;
            used_memory += curr->size + sizeof(block_header_t);
            result = (void*)((u8*)curr + sizeof(block_header_t));
            break;
        }
        prev = curr;
        curr = curr->next;
    }
    mem_unlock(flags);
    return result;
}

void kfree(void* ptr) {
    if (!initialized || !ptr) return;
    u64 flags = mem_lock();
    block_header_t* block = (block_header_t*)((u8*)ptr - sizeof(block_header_t));
    if (block->free) {
        mem_unlock(flags);
        return;
    }
    block->free = 1;
    used_memory -= block->size + sizeof(block_header_t);
    block_header_t** pp = &free_list;
    while (*pp && (u64)(*pp) < (u64)block) {
        pp = &(*pp)->next;
    }
    block->next = *pp;
    *pp = block;
    if (block->next) {
        u8* block_end = (u8*)block + sizeof(block_header_t) + block->size;
        if (block_end == (u8*)block->next) {
            block->size += sizeof(block_header_t) + block->next->size;
            block->next = block->next->next;
        }
    }
    block_header_t* prev = NULL;
    block_header_t* curr = free_list;
    while (curr && curr != block) {
        prev = curr;
        curr = curr->next;
    }
    if (prev) {
        u8* prev_end = (u8*)prev + sizeof(block_header_t) + prev->size;
        if (prev_end == (u8*)block) {
            prev->size += sizeof(block_header_t) + block->size;
            prev->next = block->next;
        }
    }
    mem_unlock(flags);
}

u64 memory_used(void) { return used_memory; }
u64 memory_free(void) { return total_memory - used_memory; }

#define PMM_PAGE_SIZE 4096

static u8* pmm_bitmap = NULL;
static u64 pmm_base = 0;
static u64 pmm_total = 0;
static u64 pmm_cursor = 0;

void pmm_init_region(u64 base, u64 size) {
    if (pmm_bitmap) return;

    u64 b = (base + PMM_PAGE_SIZE - 1) & ~(u64)(PMM_PAGE_SIZE - 1);
    u64 e = (base + size) & ~(u64)(PMM_PAGE_SIZE - 1);
    if (e <= b) return;

    u64 pages = (e - b) / PMM_PAGE_SIZE;
    u64 bitmap_bytes = (pages + 7) / 8;
    u64 bitmap_pages = (bitmap_bytes + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE;
    if (pages <= bitmap_pages + 1) return;

    pmm_bitmap = (u8*)b;
    memset(pmm_bitmap, 0, bitmap_pages * PMM_PAGE_SIZE);
    pmm_base = b + bitmap_pages * PMM_PAGE_SIZE;
    pmm_total = pages - bitmap_pages;
    pmm_cursor = 0;
}

void* pmm_alloc_page(void) {
    if (!pmm_bitmap) return NULL;
    u64 flags = mem_lock();
    void* p = NULL;
    for (u64 i = 0; i < pmm_total; i++) {
        u64 idx = (pmm_cursor + i) % pmm_total;
        u64 byte = idx / 8;
        u8 bit = (u8)(1 << (idx % 8));
        if (!(pmm_bitmap[byte] & bit)) {
            pmm_bitmap[byte] |= bit;
            pmm_cursor = (idx + 1) % pmm_total;
            p = (void*)(pmm_base + idx * PMM_PAGE_SIZE);
            break;
        }
    }
    mem_unlock(flags);
    if (p) memset(p, 0, PMM_PAGE_SIZE);
    return p;
}

void pmm_free_page(void* ptr) {
    if (!pmm_bitmap || !ptr) return;
    u64 flags = mem_lock();
    u64 addr = (u64)ptr;
    if (addr >= pmm_base) {
        u64 idx = (addr - pmm_base) / PMM_PAGE_SIZE;
        if (idx < pmm_total) {
            u64 byte = idx / 8;
            u8 bit = (u8)(1 << (idx % 8));
            pmm_bitmap[byte] &= (u8)~bit;
        }
    }
    mem_unlock(flags);
}

void* kmalloc_aligned(u64 size, u32 alignment) {
    if (alignment < 8) alignment = 8;

    u64 total_size = size + alignment + sizeof(void*);
    void* ptr = kmalloc(total_size);
    if (!ptr) return NULL;

    void** aligned_ptr = (void**)(((u64)ptr + alignment + sizeof(void*)) & ~(alignment - 1));
    aligned_ptr[-1] = ptr;

    return aligned_ptr;
}

void kfree_aligned(void* ptr) {
    if (!ptr) return;
    void** aligned_ptr = (void**)ptr;
    kfree(aligned_ptr[-1]);
}
