#ifndef CC_ELF_H
#define CC_ELF_H

#include "codegen.h"

int elf_write(const char *filename, CodeGen *cg);

#endif
