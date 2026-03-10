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

// Главная функция
void kinit_run_all(void);

// Для внутреннего использования
void kinit_register(const char* name, int (*init)(void), int priority);
void kinit_scan_sections(void);
void kinit_scan_memory(void);

#endif
