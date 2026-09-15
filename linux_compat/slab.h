#ifndef LINUX_COMPAT_SLAB_H
#define LINUX_COMPAT_SLAB_H

#include "types.h"
#include "../kernel/memory.h"
#include "../include/string.h"

#define GFP_KERNEL 0
#define GFP_ATOMIC 0
#define GFP_DMA 0

static inline void *kmalloc_linux(size_t size, int flags) {
    (void)flags;
    return kmalloc(size);
}

static inline void *kzalloc(size_t size, int flags) {
    void *p = kmalloc(size);
    if (p) memset(p, 0, size);
    return p;
}

#define kmalloc_array(n, size, flags) kmalloc_linux((n) * (size), flags)
#define kcalloc(n, size, flags) kzalloc((n) * (size), flags)
#define kvmalloc(size, flags) kmalloc_linux(size, flags)
#define kvzalloc(size, flags) kzalloc(size, flags)

static inline void kfree_linux(const void *p) {
    kfree((void*)p);
}

#define kfree(p) kfree_linux(p)
#define kvfree(p) kfree_linux(p)

#endif
