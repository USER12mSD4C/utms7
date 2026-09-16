#ifndef I915_H
#define I915_H

#include "../include/types.h"
#include "pci.h"

int i915_init(pci_dev_t* dev);
u64 i915_gem_create(u32 size);
void i915_gem_destroy(u64 handle);
void* i915_gem_map(u64 handle);
int i915_modeset(u64 fb_handle, u32 width, u32 height, u32 pitch);
int i915_present(void);

#endif
