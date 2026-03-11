#ifndef GPT_H
#define GPT_H

#include "../include/types.h"

typedef struct {
    u64 signature;
    u32 revision;
    u32 header_size;
    u32 header_crc32;
    u32 reserved;
    u64 my_lba;
    u64 alternate_lba;
    u64 first_usable_lba;
    u64 last_usable_lba;
    u8 disk_guid[16];
    u64 partition_entry_lba;
    u32 num_partition_entries;
    u32 partition_entry_size;
    u32 partition_entries_crc32;
} __attribute__((packed)) gpt_header_t;

typedef struct {
    u8 partition_guid[16];
    u8 unique_guid[16];
    u64 first_lba;
    u64 last_lba;
    u64 attributes;
    u16 name[36];
} __attribute__((packed)) gpt_entry_t;

#define GPT_GUID_EMPTY          {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}
#define GPT_GUID_UFS            {0xE2,0x13,0xB5,0xE6,0x5A,0x3A,0x49,0x4F,0x92,0xAF,0x72,0x0F,0x12,0x1F,0x6F,0x3A}
#define GPT_GUID_EFI_SYSTEM     {0x28,0x73,0x2A,0xC1,0x1F,0xF8,0xD2,0x11,0xBA,0x4B,0x00,0xA0,0xC9,0x3E,0xC9,0x3B}
#define GPT_GUID_LINUX_DATA     {0x0F,0xC6,0x3A,0xAF,0x83,0x84,0x72,0x47,0x8E,0x79,0x3D,0x69,0xD8,0x47,0x7D,0xE4}

int gpt_detect(u8 drive);
int gpt_read_partitions(u8 drive);
int gpt_create_table(u8 drive);
int gpt_add_partition(u8 drive, u64 start, u64 size, u8* guid);

#endif
