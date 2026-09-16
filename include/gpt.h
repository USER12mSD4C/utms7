#ifndef GPT_H
#define GPT_H

#include "../include/types.h"
#include "../kernel/core/vfs.h"

#define GPT_HEADER_SIGNATURE 0x5452415020494645ULL
#define GPT_PARTITION_SIGNATURE 0x0000000000000000ULL

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
    u8 padding[420];
} __attribute__((packed)) gpt_header_t;

typedef struct {
    u8 partition_type_guid[16];
    u8 unique_partition_guid[16];
    u64 starting_lba;
    u64 ending_lba;
    u64 attributes;
    u16 partition_name[36];
} __attribute__((packed)) gpt_entry_t;

int gpt_parse(vfs_node_t* dev_node, int disk_num);
int gpt_create_table(vfs_node_t* dev_node);
int gpt_add_partition(vfs_node_t* dev_node, u64 start_lba, u64 size_sectors, const u8* type_guid);

const u8* gpt_get_ufs_guid(void);
const u8* gpt_get_efi_guid(void);
const u8* gpt_get_linux_guid(void);
const u8* gpt_get_empty_guid(void);

#endif
