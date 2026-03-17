#ifndef KAPI_H
#define KAPI_H

#include "../include/types.h"

u32 kapi_memory_used(void);
u32 kapi_memory_free(void);
u32 kapi_get_ticks(void);
void kapi_yield(void);
void kapi_init(void);
#endif
