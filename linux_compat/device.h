#ifndef LINUX_COMPAT_DEVICE_H
#define LINUX_COMPAT_DEVICE_H

#include "types.h"

struct device {
    const char *name;
    void *driver_data;
};

#define dev_name(d) (((d) && (d)->name) ? (d)->name : "unknown")

static inline void *dev_get_drvdata(const struct device *dev) {
    return ((struct device*)dev)->driver_data;
}

static inline void dev_set_drvdata(struct device *dev, void *data) {
    dev->driver_data = data;
}

#endif
