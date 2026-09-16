#ifndef BINFMT_H
#define BINFMT_H
#include "../include/types.h"
typedef struct binfmt {
    const char* name;
    int (*probe)(const u8* data, u32 size);
    u64 (*load)(u8* data, u32 size, u64* pml4, u64* out_max_vaddr);
    struct binfmt* next;
} binfmt_t;
int binfmt_register(binfmt_t* fmt);
u64 binfmt_load(u8* data, u32 size, u64* pml4, u64* out_max_vaddr);
const char* binfmt_name(const u8* data, u32 size);
#endif
