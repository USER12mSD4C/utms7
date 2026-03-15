#ifndef UDISK_H
#define UDISK_H

#include "types.h"

#define UDISK_MAX_PARTITIONS 16
#define UDISK_NAME_LEN 32

typedef enum {
    PARTITION_NONE = 0,
    PARTITION_UFS,
    PARTITION_FAT32,
    PARTITION_EXT4,
    PARTITION_UNKNOWN
} partition_type_t;

typedef struct {
    u8 present;
    u8 disk_num;
    u8 partition_num;
    u64 start_lba;
    u64 end_lba;
    u64 size;
    partition_type_t type;
    char name[UDISK_NAME_LEN];
} partition_t;

typedef struct {
    u8 present;
    u8 disk_num;
    char model[41];
    u64 total_sectors;
    u32 sector_size;
    u8 partition_count;
    partition_t partitions[UDISK_MAX_PARTITIONS];
    u8 is_gpt;
} disk_info_t;

int udisk_init(void);
int udisk_scan(void);
disk_info_t* udisk_get_info(int disk_num);
partition_t* udisk_get_partition(const char* devname);
int udisk_create_mbr(int disk);
int udisk_create_gpt(int disk);
int udisk_create_partition(const char* devname, u64 size_mb, partition_type_t type);
int udisk_delete_partition(const char* devname);
int udisk_set_type(const char* devname, partition_type_t type);
int udisk_format_partition(const char* devname, const char* fstype);
int parse_devname(const char* devname, int* disk, int* part);

#endif
