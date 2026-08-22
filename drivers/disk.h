// File: drivers/disk.h
#ifndef DISK_H
#define DISK_H

#include "../include/types.h"

int disk_init(void);
int disk_read(u32 lba, u8* buffer);
int disk_write(u32 lba, u8* buffer);
int disk_set_disk(int n);
int disk_set_drive(u8 drive);
int disk_get_disk_count(void);
void disk_list_disks(void);
u64 disk_get_sectors(u8 drive);
int disk_get_boot_device(void);
int disk_init_drive(u8 drive);
void disk_get_model(u8 drive, char* model);
void ahci_register_disk(int port, u64 sectors, const char* model);

#endif
