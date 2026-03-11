#ifndef DISK_H
#define DISK_H

#include "../include/types.h"

int disk_init(void);
int disk_init_drive(u8 drive);
int disk_set_drive(u8 drive);
int disk_get_boot_device(void);
int disk_read(u32 lba, u8* buffer);
int disk_write(u32 lba, u8* buffer);
int disk_create_mbr(u8 drive);
int disk_install_grub(u8 drive);
int disk_update_boot_config(u8 drive, u32 partition_start);
int disk_set_disk(int disk_num);
int disk_get_disk_count(void);
void disk_commands_init(void);
void disk_list_disks(void);
u64 disk_get_sectors(u8 drive);

#endif
