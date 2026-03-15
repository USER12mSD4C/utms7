#include "../include/gpt.h"
#include "../include/string.h"
#include "disk.h"
#include "../drivers/vga.h"
#include "../kernel/memory.h"

// Статические GUID
static const u8 EMPTY_GUID[16] =     {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
static const u8 UFS_GUID[16] =       {0xE2,0x13,0xB5,0xE6,0x5A,0x3A,0x49,0x4F,0x92,0xAF,0x72,0x0F,0x12,0x1F,0x6F,0x3A};
static const u8 EFI_GUID[16] =       {0x28,0x73,0x2A,0xC1,0x1F,0xF8,0xD2,0x11,0xBA,0x4B,0x00,0xA0,0xC9,0x3E,0xC9,0x3B};
static const u8 LINUX_GUID[16] =     {0x0F,0xC6,0x3A,0xAF,0x83,0x84,0x72,0x47,0x8E,0x79,0x3D,0x69,0xD8,0x47,0x7D,0xE4};

// Функции доступа к GUID
const u8* gpt_get_empty_guid(void) { return EMPTY_GUID; }
const u8* gpt_get_ufs_guid(void) { return UFS_GUID; }
const u8* gpt_get_efi_guid(void) { return EFI_GUID; }
const u8* gpt_get_linux_guid(void) { return LINUX_GUID; }

static gpt_header_t gpt_header;
static gpt_entry_t gpt_entries[128];
static int gpt_valid = 0;
static int gpt_entry_count = 0;
static u8 current_drive = 0;

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
    current_drive = drive;
    
    int disk_num = drive - 0x80;
    if (disk_num < 0 || disk_num >= 4) return 0;
    disk_set_disk(disk_num);
    
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
    
    int disk_num = drive - 0x80;
    if (disk_num < 0 || disk_num >= 4) return -1;
    disk_set_disk(disk_num);
    
    u32 entries_per_sector = 512 / gpt_header.partition_entry_size;
    u32 sectors_needed = (gpt_header.num_partition_entries + entries_per_sector - 1) / entries_per_sector;
    
    gpt_entry_count = 0;
    
    for (u32 i = 0; i < sectors_needed; i++) {
        u64 lba = gpt_header.partition_entry_lba + i;
        u8 sector[512];
        
        if (disk_read(lba, sector) != 0) return -1;
        
        u32 entries_in_sector = (i == sectors_needed - 1) ? 
            (gpt_header.num_partition_entries - i * entries_per_sector) : entries_per_sector;
        
        for (u32 j = 0; j < entries_in_sector; j++) {
            if (gpt_entry_count >= 128) break;
            
            gpt_entry_t* entry = (gpt_entry_t*)(sector + j * gpt_header.partition_entry_size);
            
            if (memcmp(entry->partition_guid, gpt_get_empty_guid(), 16) == 0) {
                continue;
            }
            
            memcpy(&gpt_entries[gpt_entry_count], entry, sizeof(gpt_entry_t));
            gpt_entry_count++;
        }
    }
    
    return gpt_entry_count;
}

int gpt_get_entry(int index, gpt_entry_t* entry) {
    if (index < 0 || index >= gpt_entry_count) return -1;
    memcpy(entry, &gpt_entries[index], sizeof(gpt_entry_t));
    return 0;
}

int gpt_create_table(u8 drive) {
    u8 sector[512];
    u64 total_sectors = disk_get_sectors(drive);
    
    if (total_sectors == 0) {
        total_sectors = 976773168;
    }
    
    int disk_num = drive - 0x80;
    if (disk_num < 0 || disk_num >= 4) return -1;
    disk_set_disk(disk_num);
    
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
    
    for (int i = 0; i < 16; i++) {
        header.disk_guid[i] = (i * 0x11) ^ (total_sectors >> (i % 8));
    }
    
    init_crc32();
    header.partition_entries_crc32 = calculate_crc32((u8*)gpt_entries, 128 * 128);
    header.header_crc32 = 0;
    header.header_crc32 = calculate_crc32((u8*)&header, 92);
    
    if (disk_write(1, (u8*)&header) != 0) return -1;
    
    memset(sector, 0, 512);
    for (u64 lba = 2; lba < 34; lba++) {
        if (disk_write(lba, sector) != 0) return -1;
    }
    
    header.my_lba = total_sectors - 1;
    header.alternate_lba = 1;
    header.partition_entry_lba = total_sectors - 33;
    
    if (disk_write(total_sectors - 1, (u8*)&header) != 0) return -1;
    
    for (u64 lba = total_sectors - 33; lba < total_sectors - 1; lba++) {
        if (disk_write(lba, sector) != 0) return -1;
    }
    
    gpt_entry_count = 0;
    gpt_valid = 1;
    return 0;
}

int gpt_add_partition(u8 drive, u64 start, u64 size, const u8* guid) {
    if (!gpt_valid && !gpt_detect(drive)) return -1;
    
    int free_entry = -1;
    for (int i = 0; i < gpt_header.num_partition_entries; i++) {
        if (i >= gpt_entry_count) {
            free_entry = i;
            break;
        }
        
        if (memcmp(gpt_entries[i].partition_guid, gpt_get_empty_guid(), 16) == 0) {
            free_entry = i;
            break;
        }
    }
    
    if (free_entry == -1) return -1;
    
    gpt_entry_t new_entry;
    memset(&new_entry, 0, sizeof(gpt_entry_t));
    memcpy(new_entry.partition_guid, guid, 16);
    
    for (int i = 0; i < 16; i++) {
        new_entry.unique_guid[i] = free_entry + i + start;
    }
    
    new_entry.first_lba = start;
    new_entry.last_lba = start + size - 1;
    
    char name[] = "UTMS";
    for (int i = 0; i < 4; i++) {
        new_entry.name[i] = name[i];
    }
    
    if (free_entry >= gpt_entry_count) {
        memcpy(&gpt_entries[free_entry], &new_entry, sizeof(gpt_entry_t));
        gpt_entry_count = free_entry + 1;
    } else {
        memcpy(&gpt_entries[free_entry], &new_entry, sizeof(gpt_entry_t));
    }
    
    u32 entries_crc = calculate_crc32((u8*)gpt_entries, 
        gpt_header.num_partition_entries * gpt_header.partition_entry_size);
    gpt_header.partition_entries_crc32 = entries_crc;
    
    u32 entries_per_sector = 512 / gpt_header.partition_entry_size;
    u32 sectors_needed = (gpt_header.num_partition_entries + entries_per_sector - 1) / entries_per_sector;
    
    int disk_num = drive - 0x80;
    if (disk_num < 0 || disk_num >= 4) return -1;
    disk_set_disk(disk_num);
    
    for (u32 i = 0; i < sectors_needed; i++) {
        u8 sector[512] = {0};
        u32 entries_in_sector = (i == sectors_needed - 1) ? 
            (gpt_header.num_partition_entries - i * entries_per_sector) : entries_per_sector;
        
        for (u32 j = 0; j < entries_in_sector; j++) {
            u32 idx = i * entries_per_sector + j;
            if (idx < gpt_entry_count) {
                memcpy(sector + j * gpt_header.partition_entry_size,
                       &gpt_entries[idx],
                       gpt_header.partition_entry_size);
            }
        }
        
        if (disk_write(gpt_header.partition_entry_lba + i, sector) != 0) return -1;
        if (disk_write(gpt_header.alternate_lba - sectors_needed + i, sector) != 0) return -1;
    }
    
    gpt_header.header_crc32 = 0;
    gpt_header.header_crc32 = calculate_crc32((u8*)&gpt_header, 92);
    if (disk_write(gpt_header.my_lba, (u8*)&gpt_header) != 0) return -1;
    
    gpt_header_t backup;
    if (disk_read(gpt_header.alternate_lba, (u8*)&backup) != 0) return -1;
    backup.partition_entries_crc32 = entries_crc;
    backup.header_crc32 = 0;
    backup.header_crc32 = calculate_crc32((u8*)&backup, 92);
    if (disk_write(gpt_header.alternate_lba, (u8*)&backup) != 0) return -1;
    
    return 0;
}

int gpt_init(void) {
    gpt_valid = 0;
    gpt_entry_count = 0;
    return 0;
}
