#ifndef PANIC_H
#define PANIC_H

#include "../include/types.h"

void panic(const char* message);
void panic_assert(const char* file, u32 line, const char* expr);

#endif
