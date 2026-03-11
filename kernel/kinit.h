#ifndef KINIT_H
#define KINIT_H

#include "../include/types.h"

typedef struct {
    char name[64];
    int (*init)(void);
    int priority;
    int status;
    u64 addr;
} kinit_module_t;

// Основные функции
void kinit_run_all(void);

#endif
