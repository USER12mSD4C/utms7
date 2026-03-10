#include "../include/types.h"
#include "../include/string.h"
#include "../drivers/disk.h"
#include "../kernel/memory.h"

#define FAT12 0
#define FAT16 1
#define FAT32 2

typedef struct {
    u8 jmp[3];
    u8 oem[8];
    u16 bytes_per_sector;
    u8 sectors_per_cluster;
    u16 reserved_sectors;
    u8 fat_count;
    u16 root_entries;
    u16 total_sectors_16;
    u8 media_descriptor;
    u16 fat_size_16;
    u16 sectors_per_track;
    u16 head_count;
    u32 hidden_sectors;
    u32 total_sectors_32;
    
    // FAT32 specific
    u32 fat_size_32;
    u16 flags;
    u16 version;
    u32 root_cluster;
    u16 fs_info;
    u16 backup_boot;
    u8 reserved[12];
    u8 drive_number;
    u8 reserved1;
    u8 signature;
    u32 volume_id;
    u8 volume_label[11];
    u8 system_id[8];
} __attribute__((packed)) fat_boot_t;

typedef struct {
    u8 name[8];
    u8 ext[3];
    u8 attributes;
    u8 reserved;
    u8 create_time_tenth;
    u16 create_time;
    u16 create_date;
    u16 access_date;
    u16 cluster_high;
    u16 modify_time;
    u16 modify_date;
    u16 cluster_low;
    u32 size;
} __attribute__((packed)) fat_dir_t;

static int fat_type = 0;
static fat_boot_t boot;
static u32 fat_start = 0;
static u32 data_start = 0;
static u32 root_start = 0;
static u32 sectors_per_fat = 0;

int fat_mount(u32 start_lba) {
    u8 sector[512];
    
    if (disk_read(start_lba, sector) != 0) return -1;
    
    memcpy(&boot, sector, sizeof(fat_boot_t));
    
    if (boot.bytes_per_sector != 512) return -1;
    if (boot.signature != 0x28 && boot.signature != 0x29) return -1;
    
    // Определяем тип FAT
    u32 root_dir_sectors = ((boot.root_entries * 32) + (boot.bytes_per_sector - 1)) / boot.bytes_per_sector;
    
    if (boot.fat_size_16 != 0) {
        sectors_per_fat = boot.fat_size_16;
        fat_type = FAT16;
    } else {
        sectors_per_fat = boot.fat_size_32;
        fat_type = FAT32;
    }
    
    fat_start = start_lba + boot.reserved_sectors;
    root_start = fat_start + (boot.fat_count * sectors_per_fat);
    data_start = root_start + root_dir_sectors;
    
    return 0;
}

int fat_read_file(const char* path, u8** buffer, u32* size) {
    // Упрощенная реализация для чтения файлов
    return -1;
}
