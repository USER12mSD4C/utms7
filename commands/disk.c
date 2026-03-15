#include "disk.h"
#include "../include/shell_api.h"
#include "../include/string.h"
#include "../include/udisk.h"
#include "../fs/ufs.h"
#include "../drivers/disk.h"
#include "../drivers/vga.h"
#include "../kernel/memory.h"

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
