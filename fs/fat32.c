#include "../include/types.h"
#include "../include/string.h"
#include "../drivers/disk.h"
#include "../kernel/memory.h"

typedef struct {
    u8  jmp[3];
    u8  oem[8];
    u16 bytes_per_sector;
    u8  sectors_per_cluster;
    u16 reserved_sectors;
    u8  fat_count;
    u16 root_entries;
    u16 total_sectors_16;
    u8  media;
    u16 fat_size_16;
    u16 sectors_per_track;
    u16 head_count;
    u32 hidden_sectors;
    u32 total_sectors_32;
    u32 fat_size_32;
    u16 flags;
    u16 version;
    u32 root_cluster;
    u16 fsinfo_sector;
    u16 backup_boot;
    u8  reserved[12];
    u8  drive_number;
    u8  reserved1;
    u8  signature;
    u32 volume_id;
    u8  volume_label[11];
    u8  system_id[8];
} __attribute__((packed)) fat32_bpb_t;

typedef struct {
    u8  name[11];
    u8  attr;
    u8  nt_res;
    u8  create_time_tenth;
    u16 create_time;
    u16 create_date;
    u16 access_date;
    u16 cluster_high;
    u16 modify_time;
    u16 modify_date;
    u16 cluster_low;
    u32 size;
} __attribute__((packed)) fat32_dir_entry_t;

static u32 partition_offset = 0;
static fat32_bpb_t bpb;
static u32 first_data_sector;
static u32 root_dir_sectors;

int fat32_mount(u32 start_lba) {
    partition_offset = start_lba;
    
    u8 sector[512];
    if (disk_read(start_lba, sector) != 0) {
        return -1;
    }
    
    memcpy(&bpb, sector, sizeof(bpb));
    
    if (bpb.bytes_per_sector != 512) return -1;
    if (bpb.sectors_per_cluster == 0) return -1;
    if (bpb.fat_count == 0) return -1;
    
    root_dir_sectors = ((bpb.root_entries * 32) + (bpb.bytes_per_sector - 1)) / bpb.bytes_per_sector;
    first_data_sector = bpb.reserved_sectors + (bpb.fat_count * bpb.fat_size_32) + root_dir_sectors;
    
    return 0;
}

static u32 fat32_next_cluster(u32 cluster) {
    u32 fat_offset = cluster * 4;
    u32 fat_sector = bpb.reserved_sectors + (fat_offset / bpb.bytes_per_sector);
    u32 fat_offset_in_sector = fat_offset % bpb.bytes_per_sector;
    
    u8 sector[512];
    disk_read(partition_offset + fat_sector, sector);
    
    u32 next = *(u32*)(sector + fat_offset_in_sector);
    next &= 0x0FFFFFFF; // только 28 бит
    
    if (next >= 0x0FFFFFF8) return 0; // конец цепочки
    return next;
}

int fat32_read_file(const char *path, u8 **data, u32 *size) {
    // TODO: реализовать
    (void)path;
    (void)data;
    (void)size;
    return -1;
}
