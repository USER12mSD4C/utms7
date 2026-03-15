#include "../include/udisk.h"
#include "../include/string.h"
#include "../include/gpt.h"
#include "../fs/ufs.h"
#include "disk.h"
#include "../kernel/memory.h"
#include "../drivers/vga.h"
#include "../include/shell_api.h"

static disk_info_t disks[4];
static int scanned = 0;
static u64 last_scan_tick = 0;
extern u32 system_ticks;

// ========== ВНУТРЕННИЕ ФУНКЦИИ ==========
static void shell_print_char(char c) {
    char str[2] = {c, '\0'};
    shell_print(str);
}

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

static int mbr_add_partition(int disk, u64 start, u64 size, partition_type_t type) {
    u8 sector[512];
    disk_set_disk(disk);
    
    if (disk_read(0, sector) != 0) return -1;
    
    int free_entry = -1;
    for (int i = 0; i < 4; i++) {
        u8* entry = sector + 446 + i * 16;
        if (entry[4] == 0) {
            free_entry = i;
            break;
        }
    }
    
    if (free_entry == -1) return -1;
    
    u8* entry = sector + 446 + free_entry * 16;
    memset(entry, 0, 16);
    
    entry[0] = 0x00;
    entry[1] = 0xFF;
    entry[2] = 0xFF;
    entry[3] = 0xFF;
    
    if (type == PARTITION_UFS) {
        entry[4] = 0x83;
    } else {
        entry[4] = 0x83;
    }
    
    entry[5] = 0xFF;
    entry[6] = 0xFF;
    entry[7] = 0xFF;
    
    *(u32*)(entry + 8) = start;
    *(u32*)(entry + 12) = size;
    
    if (disk_write(0, sector) != 0) return -1;
    
    return 0;
}

// ========== API ДЛЯ ДРУГИХ ЧАСТЕЙ ЯДРА ==========

int udisk_init(void) {
    memset(disks, 0, sizeof(disks));
    scanned = 0;
    return 0;
}

int udisk_scan(void) {
    if (scanned && system_ticks - last_scan_tick < 50) {
        return 0;
    }
    
    for (int i = 0; i < 4; i++) {
        scan_disk(i);
    }
    scanned = 1;
    last_scan_tick = system_ticks;
    return 0;
}

disk_info_t* udisk_get_info(int disk_num) {
    if (disk_num < 0 || disk_num > 3) return NULL;
    udisk_scan();
    return &disks[disk_num];
}

partition_t* udisk_get_partition(const char* devname) {
    int disk, part;
    if (parse_devname(devname, &disk, &part) != 0) return NULL;
    udisk_scan();
    
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
    
    if (start_lba + size_sectors > d->total_sectors) return -1;
    
    if (d->is_gpt) {
        const u8* guid = (type == PARTITION_UFS) ? gpt_get_ufs_guid() : gpt_get_linux_guid();
        if (gpt_add_partition(0x80 + disk, start_lba, size_sectors, guid) != 0) return -1;
    } else {
        if (mbr_add_partition(disk, start_lba, size_sectors, type) != 0) return -1;
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
        u8 sector[512];
        disk_set_disk(disk);
        if (disk_read(0, sector) != 0) return -1;
        
        for (int i = 0; i < 4; i++) {
            u8* entry = sector + 446 + i * 16;
            if (entry[4] != 0 && (i + 1) == part) {
                memset(entry, 0, 16);
                break;
            }
        }
        
        if (disk_write(0, sector) != 0) return -1;
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
    
    if (d->is_gpt) {
        const u8* guid = (type == PARTITION_UFS) ? gpt_get_ufs_guid() : gpt_get_linux_guid();
        if (gpt_add_partition(0x80 + disk, p->start_lba, p->end_lba - p->start_lba + 1, guid) != 0) {
            return -1;
        }
    } else {
        u8 sector[512];
        disk_set_disk(disk);
        if (disk_read(0, sector) != 0) return -1;
        
        for (int i = 0; i < 4; i++) {
            u8* entry = sector + 446 + i * 16;
            if (entry[4] != 0 && (i + 1) == part) {
                if (type == PARTITION_UFS) {
                    entry[4] = 0x83;
                } else if (type == PARTITION_FAT32) {
                    entry[4] = 0x0C;
                }
                break;
            }
        }
        
        if (disk_write(0, sector) != 0) return -1;
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

// ========== КОМАНДЫ ШЕЛЛА ==========

static int cmd_disks(int argc, char** argv) {
    (void)argc; (void)argv;
    
    udisk_scan();
    
    for (int i = 0; i < 4; i++) {
        disk_info_t* d = udisk_get_info(i);
        if (!d || !d->present) continue;
        
        char name[16] = "/dev/sdX";
        name[7] = 'a' + i;
        
        shell_print(name);
        shell_print("  ");
        shell_print_num(d->total_sectors * 512 / (1024*1024));
        shell_print(" MB  ");
        shell_print(d->model);
        shell_print(d->is_gpt ? "  GPT" : "  MBR");
        shell_print("\n");
        
        for (int j = 0; j < d->partition_count; j++) {
            partition_t* p = &d->partitions[j];
            if (!p->present) continue;
            
            char pname[16] = "/dev/sdX";
            pname[7] = 'a' + i;
            
            if (p->partition_num < 10) {
                pname[8] = '0' + p->partition_num;
                pname[9] = '\0';
            } else {
                pname[8] = '0' + p->partition_num / 10;
                pname[9] = '0' + p->partition_num % 10;
                pname[10] = '\0';
            }
            
            shell_print("  ");
            shell_print(pname);
            shell_print("  ");
            shell_print_num(p->size / (1024*1024));
            shell_print(" MB  ");
            
            switch(p->type) {
                case PARTITION_UFS: shell_print("UFS"); break;
                case PARTITION_FAT32: shell_print("FAT32"); break;
                case PARTITION_EXT4: shell_print("EXT4"); break;
                default: shell_print("unknown"); break;
            }
            
            if (p->name[0]) {
                shell_print("  ");
                shell_print(p->name);
            }
            shell_print("\n");
        }
    }
    
    return 0;
}

static int cmd_lsblk(int argc, char** argv) {
    return cmd_disks(argc, argv);
}

static int cmd_udisk(int argc, char** argv) {
    if (argc < 2) {
        shell_print("Usage: udisk <cmd> [args]\n");
        shell_print("Commands:\n");
        shell_print("  list                    - show disks\n");
        shell_print("  mbr /dev/sdX            - create MBR table\n");
        shell_print("  gpt /dev/sdX            - create GPT table\n");
        shell_print("  create /dev/sdX <size>  - create partition\n");
        shell_print("  delete /dev/sdX[1-16]   - delete partition\n");
        shell_print("  type /dev/sdX[1-16] <type> - set partition type\n");
        shell_print("Types: ufs, fat32, ext4\n");
        return -1;
    }
    
    if (strcmp(argv[1], "list") == 0) {
        return cmd_disks(argc, argv);
    }
    
    if (strcmp(argv[1], "mbr") == 0) {
        if (argc < 3) {
            shell_print("Usage: udisk mbr /dev/sdX\n");
            return -1;
        }
        
        int disk, part;
        if (parse_devname(argv[2], &disk, &part) != 0 || part != 0) {
            shell_print("invalid disk\n");
            return -1;
        }
        
        shell_print("Creating MBR on ");
        shell_print(argv[2]);
        shell_print("... ");
        
        if (udisk_create_mbr(disk) == 0) {
            shell_print("OK\n");
            return 0;
        } else {
            shell_print("FAILED\n");
            return -1;
        }
    }
    
    if (strcmp(argv[1], "gpt") == 0) {
        if (argc < 3) {
            shell_print("Usage: udisk gpt /dev/sdX\n");
            return -1;
        }
        
        int disk, part;
        if (parse_devname(argv[2], &disk, &part) != 0 || part != 0) {
            shell_print("invalid disk\n");
            return -1;
        }
        
        shell_print("Creating GPT on ");
        shell_print(argv[2]);
        shell_print("... ");
        
        if (udisk_create_gpt(disk) == 0) {
            shell_print("OK\n");
            return 0;
        } else {
            shell_print("FAILED\n");
            return -1;
        }
    }
    
    if (strcmp(argv[1], "create") == 0) {
        if (argc < 4) {
            shell_print("Usage: udisk create /dev/sdX <sizeMB>\n");
            return -1;
        }
        
        int disk, part;
        if (parse_devname(argv[2], &disk, &part) != 0 || part != 0) {
            shell_print("invalid disk\n");
            return -1;
        }
        
        u32 size_mb = 0;
        char* p = argv[3];
        while (*p) {
            if (*p < '0' || *p > '9') {
                shell_print("invalid size\n");
                return -1;
            }
            size_mb = size_mb * 10 + (*p - '0');
            p++;
        }
        
        shell_print("Creating partition on ");
        shell_print(argv[2]);
        shell_print(" size ");
        shell_print_num(size_mb);
        shell_print(" MB... ");
        
        if (udisk_create_partition(argv[2], size_mb, PARTITION_UFS) == 0) {
            shell_print("OK\n");
            return 0;
        } else {
            shell_print("FAILED\n");
            return -1;
        }
    }
    
    if (strcmp(argv[1], "delete") == 0) {
        if (argc < 3) {
            shell_print("Usage: udisk delete /dev/sdX[1-16]\n");
            return -1;
        }
        
        partition_t* p = udisk_get_partition(argv[2]);
        if (!p) {
            shell_print("partition not found\n");
            return -1;
        }
        
        shell_print("Deleting ");
        shell_print(argv[2]);
        shell_print("... ");
        
        if (udisk_delete_partition(argv[2]) == 0) {
            shell_print("OK\n");
            return 0;
        } else {
            shell_print("FAILED\n");
            return -1;
        }
    }
    
    if (strcmp(argv[1], "type") == 0) {
        if (argc < 4) {
            shell_print("Usage: udisk type /dev/sdX[1-16] <type>\n");
            return -1;
        }
        
        partition_t* p = udisk_get_partition(argv[2]);
        if (!p) {
            shell_print("partition not found\n");
            return -1;
        }
        
        partition_type_t type;
        if (strcmp(argv[3], "ufs") == 0) type = PARTITION_UFS;
        else if (strcmp(argv[3], "fat32") == 0) type = PARTITION_FAT32;
        else if (strcmp(argv[3], "ext4") == 0) type = PARTITION_EXT4;
        else {
            shell_print("unknown type\n");
            return -1;
        }
        
        shell_print("Setting type of ");
        shell_print(argv[2]);
        shell_print(" to ");
        shell_print(argv[3]);
        shell_print("... ");
        
        if (udisk_set_type(argv[2], type) == 0) {
            shell_print("OK\n");
            return 0;
        } else {
            shell_print("FAILED\n");
            return -1;
        }
    }
    
    shell_print("unknown udisk command\n");
    return -1;
}

static int cmd_mkfs_ufs(int argc, char** argv) {
    if (argc < 2) {
        shell_print("Usage: mkfs.ufs /dev/sdX[1-16]\n");
        return -1;
    }
    
    partition_t* p = udisk_get_partition(argv[1]);
    if (!p) {
        shell_print("invalid partition\n");
        return -1;
    }
    
    shell_print("Formatting ");
    shell_print(argv[1]);
    shell_print("... ");
    
    if (udisk_format_partition(argv[1], "ufs") == 0) {
        shell_print("OK\n");
        return 0;
    } else {
        shell_print("FAILED\n");
        return -1;
    }
}

static int cmd_mount(int argc, char** argv) {
    if (argc < 2) {
        shell_print("Usage: mount /dev/sdX[1-16]\n");
        return -1;
    }
    
    partition_t* p = udisk_get_partition(argv[1]);
    if (!p) {
        shell_print("invalid partition\n");
        return -1;
    }
    
    if (ufs_ismounted()) {
        shell_print("already mounted\n");
        return -1;
    }
    
    shell_print("Mounting ");
    shell_print(argv[1]);
    shell_print("... ");
    
    if (ufs_mount(p->start_lba, p->disk_num) == 0) {
        shell_print("OK\n");
        return 0;
    } else {
        shell_print("FAILED\n");
        return -1;
    }
}

static int cmd_umount(int argc, char** argv) {
    (void)argc; (void)argv;
    
    if (!ufs_ismounted()) {
        shell_print("not mounted\n");
        return -1;
    }
    
    if (ufs_umount() == 0) {
        shell_print("unmounted\n");
        return 0;
    }
    return -1;
}

void disk_commands_init(void) {
    shell_register_command("disks", cmd_disks, "list all disks");
    shell_register_command("lsblk", cmd_lsblk, "list block devices");
    shell_register_command("udisk", cmd_udisk, "partition manager");
    shell_register_command("mkfs.ufs", cmd_mkfs_ufs, "format partition");
    shell_register_command("mount", cmd_mount, "mount ufs partition");
    shell_register_command("umount", cmd_umount, "unmount ufs");
}
