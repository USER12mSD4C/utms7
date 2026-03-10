#include "../include/gpt.h"
#include "../include/string.h"
#include "disk.h"
#include "../drivers/vga.h"

static gpt_header_t gpt_header;
static gpt_entry_t gpt_entries[128];
static int gpt_valid = 0;

static u32 crc32_table[256];

static void init_crc32(void) {
    for (u32 i = 0; i < 256; i++) {
        u32 crc = i;
        for (u32 j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
        crc32_table[i] = crc;
    }
}

static u32 calculate_crc32(u8* data, u32 len) {
    u32 crc = 0xFFFFFFFF;
    for (u32 i = 0; i < len; i++) {
        crc = (crc >> 8) ^ crc32_table[(crc ^ data[i]) & 0xFF];
    }
    return crc ^ 0xFFFFFFFF;
}

int gpt_detect(u8 drive) {
    u8 sector[512];
    disk_set_drive(drive);
    
    if (disk_read(0, sector) != 0) return 0;
    if (sector[510] != 0x55 || sector[511] != 0xAA) return 0;
    
    int gpt_protective = 0;
    for (int i = 0; i < 4; i++) {
        if (sector[446 + i*16 + 4] == 0xEE) {
            gpt_protective = 1;
            break;
        }
    }
    if (!gpt_protective) return 0;
    
    if (disk_read(1, (u8*)&gpt_header) != 0) return 0;
    if (gpt_header.signature != 0x5452415020494645ULL) return 0;
    
    u32 saved_crc = gpt_header.header_crc32;
    gpt_header.header_crc32 = 0;
    
    init_crc32();
    u32 calc_crc = calculate_crc32((u8*)&gpt_header, gpt_header.header_size);
    
    if (calc_crc != saved_crc) return 0;
    
    gpt_valid = 1;
    return 1;
}

int gpt_read_partitions(u8 drive) {
    if (!gpt_valid && !gpt_detect(drive)) return -1;
    
    disk_set_drive(drive);
    
    u32 entries_per_sector = 512 / gpt_header.partition_entry_size;
    u32 sectors_needed = (gpt_header.num_partition_entries + entries_per_sector - 1) / entries_per_sector;
    
    for (u32 i = 0; i < sectors_needed; i++) {
        u64 lba = gpt_header.partition_entry_lba + i;
        u8 sector[512];
        
        if (disk_read(lba, sector) != 0) return -1;
        
        u32 entries_in_sector = (i == sectors_needed - 1) ? 
            (gpt_header.num_partition_entries - i * entries_per_sector) : entries_per_sector;
        
        for (u32 j = 0; j < entries_in_sector; j++) {
            gpt_entry_t* entry = (gpt_entry_t*)(sector + j * gpt_header.partition_entry_size);
            memcpy(&gpt_entries[i * entries_per_sector + j], entry, sizeof(gpt_entry_t));
        }
    }
    
    return gpt_header.num_partition_entries;
}

int gpt_create_table(u8 drive) {
    u8 sector[512];
    u64 total_sectors = disk_get_sectors(drive);
    if (total_sectors == 0) total_sectors = 976773168;
    
    disk_set_drive(drive);
    
    memset(sector, 0, 512);
    sector[510] = 0x55;
    sector[511] = 0xAA;
    
    sector[446] = 0x00;
    sector[447] = 0x00;
    sector[448] = 0x02;
    sector[449] = 0x00;
    sector[450] = 0xEE;
    sector[451] = 0xFF;
    sector[452] = 0xFF;
    sector[453] = 0xFF;
    
    sector[454] = 0x01;
    sector[455] = 0x00;
    sector[456] = 0x00;
    sector[457] = 0x00;
    
    u32 size = total_sectors - 1;
    sector[458] = size & 0xFF;
    sector[459] = (size >> 8) & 0xFF;
    sector[460] = (size >> 16) & 0xFF;
    sector[461] = (size >> 24) & 0xFF;
    
    if (disk_write(0, sector) != 0) return -1;
    
    gpt_header_t header;
    memset(&header, 0, sizeof(header));
    header.signature = 0x5452415020494645ULL;
    header.revision = 0x00010000;
    header.header_size = 92;
    header.my_lba = 1;
    header.alternate_lba = total_sectors - 1;
    header.first_usable_lba = 34;
    header.last_usable_lba = total_sectors - 34;
    header.partition_entry_lba = 2;
    header.num_partition_entries = 128;
    header.partition_entry_size = 128;
    
    init_crc32();
    header.header_crc32 = 0;
    header.header_crc32 = calculate_crc32((u8*)&header, 92);
    
    if (disk_write(1, (u8*)&header) != 0) return -1;
    
    memset(sector, 0, 512);
    for (u64 lba = 2; lba < 34; lba++) {
        if (disk_write(lba, sector) != 0) return -1;
    }
    
    return 0;
}

int gpt_add_partition(u8 drive, u64 start, u64 size, u8* guid) {
    if (!gpt_valid && !gpt_detect(drive)) return -1;
    
    int free_entry = -1;
    for (int i = 0; i < gpt_header.num_partition_entries; i++) {
        if (gpt_entries[i].first_lba == 0 && gpt_entries[i].last_lba == 0) {
            free_entry = i;
            break;
        }
    }
    if (free_entry == -1) return -1;
    
    memset(&gpt_entries[free_entry], 0, sizeof(gpt_entry_t));
    memcpy(gpt_entries[free_entry].partition_guid, guid, 16);
    
    for (int i = 0; i < 16; i++) {
        gpt_entries[free_entry].unique_guid[i] = free_entry + i + start;
    }
    
    gpt_entries[free_entry].first_lba = start;
    gpt_entries[free_entry].last_lba = start + size - 1;
    
    init_crc32();
    u32 entries_crc = calculate_crc32((u8*)gpt_entries, 
        gpt_header.num_partition_entries * gpt_header.partition_entry_size);
    gpt_header.partition_entries_crc32 = entries_crc;
    
    u32 entries_per_sector = 512 / gpt_header.partition_entry_size;
    u32 sectors_needed = (gpt_header.num_partition_entries + entries_per_sector - 1) / entries_per_sector;
    
    for (u32 i = 0; i < sectors_needed; i++) {
        u8 sector[512] = {0};
        u32 entries_in_sector = (i == sectors_needed - 1) ? 
            (gpt_header.num_partition_entries - i * entries_per_sector) : entries_per_sector;
        
        for (u32 j = 0; j < entries_in_sector; j++) {
            memcpy(sector + j * gpt_header.partition_entry_size,
                   &gpt_entries[i * entries_per_sector + j],
                   gpt_header.partition_entry_size);
        }
        
        if (disk_write(gpt_header.partition_entry_lba + i, sector) != 0) return -1;
    }
    
    gpt_header.header_crc32 = 0;
    gpt_header.header_crc32 = calculate_crc32((u8*)&gpt_header, 92);
    if (disk_write(gpt_header.my_lba, (u8*)&gpt_header) != 0) return -1;
    
    return 0;
}
