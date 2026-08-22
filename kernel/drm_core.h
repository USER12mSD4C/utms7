#ifndef DRM_CORE_H
#define DRM_CORE_H

#include "../include/types.h"

#define DRM_MAX_DEVICES 4

typedef struct drm_device_core {
    int id;
    char name[32];
    void* private;
    int (*ioctl)(struct drm_device_core* dev, unsigned int cmd, unsigned long arg);
    int (*mmap)(struct drm_device_core* dev, u64 offset, u64 size, u64* phys);
} drm_device_core_t;

int drm_core_init(void);
int drm_core_register_device(drm_device_core_t* dev);
drm_device_core_t* drm_core_get_device(int id);

#endif
