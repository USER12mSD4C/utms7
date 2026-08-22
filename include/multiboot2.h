#ifndef MULTIBOOT2_H
#define MULTIBOOT2_H

#include "types.h"

#define MULTIBOOT2_MAGIC         0x36d76289
#define MULTIBOOT2_TAG_END       0
#define MULTIBOOT2_TAG_MMAP      6
#define MULTIBOOT2_MEMORY_AVAILABLE 1
#define MULTIBOOT2_MEMORY_RESERVED  2

typedef struct {
    u32 total_size;
    u32 reserved;
} __attribute__((packed)) multiboot2_info_header_t;

typedef struct {
    u32 type;
    u32 size;
} __attribute__((packed)) multiboot2_tag_t;

typedef struct {
    u32 type;
    u32 size;
    u32 entry_size;
    u32 entry_version;
} __attribute__((packed)) multiboot2_tag_mmap_t;

typedef struct {
    u64 base_addr;
    u64 length;
    u32 type;
    u32 reserved;
} __attribute__((packed)) multiboot2_mmap_entry_t;

#endif
