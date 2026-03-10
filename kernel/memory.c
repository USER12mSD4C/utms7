#include "memory.h"
#include "../include/io.h"
#include "../include/string.h"

static u8 memory_pool[MEMORY_SIZE] __attribute__((aligned(16)));
static block_header_t* free_list = NULL;

void memory_init(u64 mem_start, u64 mem_size) {
    (void)mem_start;
    (void)mem_size;
    
    free_list = (block_header_t*)memory_pool;
    free_list->size = MEMORY_SIZE - sizeof(block_header_t);
    free_list->next = NULL;
    free_list->free = 1;
}

void* kmalloc(u64 size) {
    if (size == 0) return NULL;
    
    size = (size + 7) & ~7;
    
    block_header_t* current = free_list;
    
    while (current) {
        if (current->free && current->size >= size) {
            if (current->size >= size + sizeof(block_header_t) + 8) {
                block_header_t* new = (block_header_t*)((u8*)current + sizeof(block_header_t) + size);
                new->size = current->size - size - sizeof(block_header_t);
                new->next = current->next;
                new->free = 1;
                
                current->size = size;
                current->next = new;
            }
            
            current->free = 0;
            return (void*)((u8*)current + sizeof(block_header_t));
        }
        
        current = current->next;
    }
    
    return NULL;
}

void kfree(void* ptr) {
    if (!ptr) return;
    
    block_header_t* block = (block_header_t*)((u8*)ptr - sizeof(block_header_t));
    block->free = 1;
    
    block_header_t* current = free_list;
    while (current) {
        if (current->free && current->next && current->next->free) {
            current->size += sizeof(block_header_t) + current->next->size;
            current->next = current->next->next;
        }
        current = current->next;
    }
}

u64 memory_used(void) {
    u64 used = 0;
    block_header_t* current = free_list;
    
    while (current) {
        if (!current->free) {
            used += current->size + sizeof(block_header_t);
        }
        current = current->next;
    }
    
    return used;
}

u64 memory_free(void) {
    u64 free_mem = 0;
    block_header_t* current = free_list;
    
    while (current) {
        if (current->free) {
            free_mem += current->size + sizeof(block_header_t);
        }
        current = current->next;
    }
    
    return free_mem;
}
