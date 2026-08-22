#ifndef GPT_H
#define GPT_H

#include "../include/types.h"

// GPT Header
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

// GPT Partition Entry
typedef struct {
    u8 partition_guid[16];
    u8 unique_guid[16];
    u64 first_lba;
    u64 last_lba;
    u64 attributes;
    u16 name[36];
} __attribute__((packed)) gpt_entry_t;

// Прототипы функций
int gpt_init(void);
int gpt_detect(u8 drive);
int gpt_create_table(u8 drive);
int gpt_add_partition(u8 drive, u64 start, u64 size, const u8* guid);
int gpt_read_partitions(u8 drive);
int gpt_get_entry(int index, gpt_entry_t* entry);

// Вспомогательные функции для получения GUID
const u8* gpt_get_empty_guid(void);
const u8* gpt_get_ufs_guid(void);
const u8* gpt_get_efi_guid(void);
const u8* gpt_get_linux_guid(void);

#endif
