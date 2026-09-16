#ifndef FS_AUTO_H
#define FS_AUTO_H

#include "types.h"

typedef int (*fs_init_func_t)(void);

typedef struct {
    const char* name;
    fs_init_func_t init;
} __attribute__((packed)) fs_init_entry_t;

#define FS_REGISTER(fs_name, init_func) \
    static fs_init_entry_t __fs_##init_func##_entry \
    __attribute__((section(".fs_init"), used)) = { \
        .name = fs_name, \
        .init = init_func \
    }

#endif
