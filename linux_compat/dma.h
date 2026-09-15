#ifndef LINUX_COMPAT_DMA_H
#define LINUX_COMPAT_DMA_H

#include "types.h"
#include "device.h"

typedef u64 dma_addr_t;
typedef u64 gfp_t;

#define DMA_BIT_MASK(n) (((n) == 64) ? ~0ULL : ((1ULL << (n)) - 1))
#define DMA_FROM_DEVICE 1
#define DMA_TO_DEVICE 2

void *dma_alloc_coherent(struct device *dev, size_t size, dma_addr_t *dma_handle, gfp_t gfp);
void dma_free_coherent(struct device *dev, size_t size, void *vaddr, dma_addr_t dma_handle);
dma_addr_t dma_map_single(struct device *dev, void *ptr, size_t size, int dir);
void dma_unmap_single(struct device *dev, dma_addr_t addr, size_t size, int dir);
int dma_set_mask_and_coherent(struct device *dev, u64 mask);

#endif
