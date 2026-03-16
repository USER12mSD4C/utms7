#ifndef KAPI_H
#define KAPI_H

#include "types.h"

// Системные вызовы
void kapi_init(void);

// Для использования в ядре
long syscall_handler_c(long num, long a1, long a2, long a3, long a4, long a5, long a6);

#endif
