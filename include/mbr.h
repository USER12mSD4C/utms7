// File: drivers/mbr.h
#ifndef MBR_H
#define MBR_H

#include "../include/types.h"

typedef struct {
    u8 bootable;
    u8 chs_start[3];
    u8 type;
    u8 chs_end[3];
    u32 lba_start;
    u32 lba_count;
} __attribute__((packed)) mbr_entry_t;

int mbr_detect(u8 drive);
int mbr_create(u8 drive);
int mbr_read_partitions(u8 drive);
int mbr_get_entry(int index, mbr_entry_t* entry);
int mbr_add_partition(u8 drive, u64 start_lba, u64 size_lba, u8 type);
int mbr_delete_partition(u8 drive, int index);
int mbr_set_type(u8 drive, int index, u8 type);

#endif
