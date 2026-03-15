#include "../include/udisk.h"
#include "../include/string.h"
#include "../include/gpt.h"
#include "../fs/ufs.h"
#include "disk.h"
#include "../kernel/memory.h"
#include "../drivers/vga.h"

static disk_info_t disks[4];
static int scanned = 0;

int parse_devname(const char* devname, int* disk, int* part) {
    if (!devname) return -1;
    
    if (devname[0] != '/' || devname[1] != 'd' || devname[2] != 'e' || 
        devname[3] != 'v' || devname[4] != '/' || devname[5] != 's' || 
        devname[6] != 'd') {
        return -1;
    }
    
    *disk = devname[7] - 'a';
    if (*disk < 0 || *disk > 3) return -1;
    
    if (devname[8] == '\0') {
        *part = 0;
    } else {
        *part = 0;
        for (int i = 8; devname[i] >= '0' && devname[i] <= '9'; i++) {
            *part = *part * 10 + (devname[i] - '0');
        }
    }
    
    return 0;
}

static void read_mbr_partitions(int disk_num, disk_info_t* d) {
    u8 sector[512];
    
    disk_set_disk(disk_num);
    if (disk_read(0, sector) != 0) return;
    if (sector[510] != 0x55 || sector[511] != 0xAA) return;
    
    d->is_gpt = 0;
    d->partition_count = 0;
    
    for (int i = 0; i < 4; i++) {
        u8* entry = sector + 446 + i * 16;
        u8 type = entry[4];
        if (type == 0) continue;
        
        partition_t* p = &d->partitions[d->partition_count];
        p->present = 1;
        p->disk_num = disk_num;
        p->partition_num = i + 1;
        p->start_lba = *(u32*)(entry + 8);
        p->end_lba = p->start_lba + *(u32*)(entry + 12) - 1;
        p->size = *(u32*)(entry + 12) * 512;
        
        if (type == 0x83) p->type = PARTITION_UFS;
        else if (type == 0x0B || type == 0x0C) p->type = PARTITION_FAT32;
        else p->type = PARTITION_UNKNOWN;
        
        snprintf(p->name, UDISK_NAME_LEN, "Partition %d", i+1);
        d->partition_count++;
    }
}

static void read_gpt_partitions(int disk_num, disk_info_t* d) {
    u8 drive = 0x80 + disk_num;
    
    if (!gpt_detect(drive)) return;
    
    int count = gpt_read_partitions(drive);
    if (count <= 0) return;
    
    d->is_gpt = 1;
    d->partition_count = (count < UDISK_MAX_PARTITIONS) ? count : UDISK_MAX_PARTITIONS;
    
    for (int i = 0; i < d->partition_count; i++) {
        partition_t* p = &d->partitions[i];
        gpt_entry_t entry;
        
        if (gpt_get_entry(i, &entry) != 0) {
            p->present = 0;
            continue;
        }
        
        p->present = 1;
        p->disk_num = disk_num;
        p->partition_num = i + 1;
        p->start_lba = entry.first_lba;
        p->end_lba = entry.last_lba;
        p->size = (entry.last_lba - entry.first_lba + 1) * 512;
        
        if (memcmp(entry.partition_guid, gpt_get_ufs_guid(), 16) == 0) {
            p->type = PARTITION_UFS;
        } else if (memcmp(entry.partition_guid, gpt_get_empty_guid(), 16) == 0) {
            p->present = 0;
        } else {
            p->type = PARTITION_UNKNOWN;
        }
        
        for (int j = 0; j < 36 && j < UDISK_NAME_LEN-1; j++) {
            p->name[j] = entry.name[j] & 0xFF;
            if (p->name[j] == 0) break;
        }
        p->name[UDISK_NAME_LEN-1] = '\0';
    }
}

static void scan_disk(int disk_num) {
    disk_info_t* d = &disks[disk_num];
    memset(d, 0, sizeof(disk_info_t));
    
    u8 drive = 0x80 + disk_num;
    u64 sectors = disk_get_sectors(drive);
    if (sectors == 0) return;
    
    d->present = 1;
    d->disk_num = disk_num;
    d->total_sectors = sectors;
    d->sector_size = 512;
    
    u8 identify[512];
    disk_set_disk(disk_num);
    if (disk_read(0, identify) == 0) {
        for (int i = 0; i < 40; i+=2) {
            d->model[i] = identify[54 + i/2*2 + 1];
            d->model[i+1] = identify[54 + i/2*2];
        }
        d->model[40] = '\0';
        
        for (int i = 0; i < 40; i++) {
            if (d->model[i] < 32 || d->model[i] > 126) {
                d->model[i] = ' ';
            }
        }
    } else {
        strcpy(d->model, "Unknown");
    }
    
    read_gpt_partitions(disk_num, d);
    if (d->partition_count == 0) {
        read_mbr_partitions(disk_num, d);
    }
}

int udisk_init(void) {
    memset(disks, 0, sizeof(disks));
    scanned = 0;
    return 0;
}

int udisk_scan(void) {
    for (int i = 0; i < 4; i++) {
        scan_disk(i);
    }
    scanned = 1;
    return 0;
}

disk_info_t* udisk_get_info(int disk_num) {
    if (disk_num < 0 || disk_num > 3) return NULL;
    if (!scanned) udisk_scan();
    return &disks[disk_num];
}

partition_t* udisk_get_partition(const char* devname) {
    int disk, part;
    if (parse_devname(devname, &disk, &part) != 0) return NULL;
    if (!scanned) udisk_scan();
    
    if (part == 0) return NULL;
    
    disk_info_t* d = &disks[disk];
    if (!d->present) return NULL;
    
    for (int i = 0; i < d->partition_count; i++) {
        if (d->partitions[i].present && d->partitions[i].partition_num == part) {
            return &d->partitions[i];
        }
    }
    return NULL;
}

int udisk_create_mbr(int disk) {
    u8 sector[512];
    disk_set_disk(disk);
    memset(sector, 0, 512);
    sector[510] = 0x55;
    sector[511] = 0xAA;
    
    if (disk_write(0, sector) != 0) return -1;
    
    scanned = 0;
    return 0;
}

int udisk_create_gpt(int disk) {
    if (gpt_create_table(0x80 + disk) != 0) return -1;
    scanned = 0;
    return 0;
}

int udisk_create_partition(const char* devname, u64 size_mb, partition_type_t type) {
    int disk, part;
    if (parse_devname(devname, &disk, &part) != 0 || part != 0) return -1;
    
    disk_info_t* d = udisk_get_info(disk);
    if (!d || !d->present) return -1;
    
    u64 size_sectors = (size_mb * 1024 * 1024) / 512;
    u64 start_lba = 2048;
    
    for (int i = 0; i < d->partition_count; i++) {
        if (d->partitions[i].present) {
            if (d->partitions[i].end_lba + 1 > start_lba) {
                start_lba = d->partitions[i].end_lba + 1;
            }
        }
    }
    
    if (start_lba + size_sectors > d->total_sectors - 34) return -1;
    
    const u8* guid;
    if (type == PARTITION_UFS) {
        guid = gpt_get_ufs_guid();
    } else {
        guid = gpt_get_linux_guid();
    }
    
    if (d->is_gpt) {
        if (gpt_add_partition(0x80 + disk, start_lba, size_sectors, guid) != 0) return -1;
    } else {
        return -1;
    }
    
    scanned = 0;
    udisk_scan();
    return 0;
}

int udisk_delete_partition(const char* devname) {
    int disk, part;
    if (parse_devname(devname, &disk, &part) != 0 || part == 0) return -1;
    
    disk_info_t* d = udisk_get_info(disk);
    if (!d || !d->present) return -1;
    
    if (d->is_gpt) {
        if (gpt_add_partition(0x80 + disk, 0, 0, gpt_get_empty_guid()) != 0) return -1;
    } else {
        return -1;
    }
    
    scanned = 0;
    udisk_scan();
    return 0;
}

int udisk_set_type(const char* devname, partition_type_t type) {
    int disk, part;
    if (parse_devname(devname, &disk, &part) != 0 || part == 0) return -1;
    
    partition_t* p = udisk_get_partition(devname);
    if (!p) return -1;
    
    disk_info_t* d = &disks[disk];
    
    const u8* guid;
    if (type == PARTITION_UFS) {
        guid = gpt_get_ufs_guid();
    } else {
        guid = gpt_get_linux_guid();
    }
    
    if (d->is_gpt) {
        if (gpt_add_partition(0x80 + disk, p->start_lba, p->end_lba - p->start_lba + 1, guid) != 0) {
            return -1;
        }
    } else {
        return -1;
    }
    
    scanned = 0;
    udisk_scan();
    return 0;
}

int udisk_format_partition(const char* devname, const char* fstype) {
    partition_t* p = udisk_get_partition(devname);
    if (!p) return -1;
    
    if (strcmp(fstype, "ufs") == 0) {
        u32 blocks = (p->end_lba - p->start_lba + 1);
        return ufs_format(p->start_lba, blocks, p->disk_num);
    }
    
    return -1;
}
