#ifndef DISK_H
#define DISK_H

#include "../include/types.h"

int disk_init(void);
int disk_read(u32 lba, u8* buffer);
int disk_write(u32 lba, u8* buffer);
int disk_set_disk(int disk_num);
int disk_get_disk_count(void);
void disk_list_disks(void);
u64 disk_get_sectors(u8 drive);

#endif
