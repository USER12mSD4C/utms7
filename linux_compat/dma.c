#include "types.h"
#include "dma.h"
#include "../kernel/core/memory.h"
#include "../include/string.h"

void *dma_alloc_coherent(struct device *dev, size_t size, dma_addr_t *dma_handle, gfp_t gfp) {
    (void)dev; (void)gfp;
    if (size == 0) return NULL;
    void *p = kmalloc_aligned(size, 4096);
    if (!p) return NULL;
    memset(p, 0, size);
    if (dma_handle) *dma_handle = (dma_addr_t)(u64)p;
    return p;
}

void dma_free_coherent(struct device *dev, size_t size, void *vaddr, dma_addr_t dma_handle) {
    (void)dev; (void)size; (void)dma_handle;
    if (vaddr) kfree_aligned(vaddr);
}

dma_addr_t dma_map_single(struct device *dev, void *ptr, size_t size, int dir) {
    (void)dev; (void)size; (void)dir;
    return (dma_addr_t)(u64)ptr;
}

void dma_unmap_single(struct device *dev, dma_addr_t addr, size_t size, int dir) {
    (void)dev; (void)addr; (void)size; (void)dir;
}

int dma_set_mask_and_coherent(struct device *dev, u64 mask) {
    (void)dev; (void)mask;
    return 0;
}
