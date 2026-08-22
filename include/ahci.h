// File: include/ahci.h
#ifndef AHCI_H
#define AHCI_H

#include "../include/types.h"

int ahci_init(void);
int ahci_read(int port, u64 lba, u32 count, void* buffer);
int ahci_write(int port, u64 lba, u32 count, void* buffer);

#endif
