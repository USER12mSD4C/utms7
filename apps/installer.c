#include "../include/string.h"
#include "../drivers/vga.h"
#include "../drivers/disk.h"
#include "../drivers/keyboard.h"
#include "../fs/ufs.h"
#include "../include/udisk.h"
#include "../kernel/memory.h"
#include "../include/shell_api.h"

static void create_directories(void) {
    ufs_mkdir("/boot");
    ufs_mkdir("/boot/grub");
    ufs_mkdir("/modules");
    ufs_mkdir("/bin");
    ufs_mkdir("/etc");
    ufs_mkdir("/home");
    ufs_mkdir("/usr");
    ufs_mkdir("/docs");
}

static void copy_file(const char* src, const char* dst) {
    u8* data;
    u32 size;
    if (ufs_read(src, &data, &size) != 0) {
        shell_print("  missing: "); shell_print(src); shell_print("\n");
        return;
    }
    if (ufs_write(dst, data, size) == 0) {
        shell_print("  "); shell_print(dst); shell_print("\n");
    }
    kfree(data);
}

static void copy_kernel(void) {
    shell_print("  kernel...\n");
    copy_file("/system/boot/kernel.bin", "/boot/kernel.bin");
}

static void copy_modules(void) {
    FSNode* entries;
    u32 count;
    if (ufs_readdir("/system/modules", &entries, &count) != 0) {
        shell_print("  no modules\n");
        return;
    }
    
    for (u32 i = 0; i < count; i++) {
        if (entries[i].is_dir) continue;
        
        char src[256];
        char dst[256];
        snprintf(src, sizeof(src), "/system/modules/%s", entries[i].name);
        snprintf(dst, sizeof(dst), "/modules/%s", entries[i].name);
        copy_file(src, dst);
    }
    
    kfree(entries);
}

static void create_grub_cfg(void) {
    char* cfg = 
        "set timeout=5\n"
        "set default=0\n"
        "menuentry \"UTMS\" {\n"
        "    multiboot2 /boot/kernel.bin\n"
        "    boot\n"
        "}\n";
    
    ufs_write("/boot/grub/grub.cfg", (u8*)cfg, strlen(cfg));
    shell_print("  grub.cfg\n");
}

int install_main(int argc, char** argv) {
    (void)argc; (void)argv;
    
    shell_print("\nUTMS Installer\n");
    shell_print("==============\n\n");
    
    udisk_scan();
    shell_print("Available disks:\n");
    
    for (int i = 0; i < 4; i++) {
        disk_info_t* d = udisk_get_info(i);
        if (!d || !d->present) continue;
        
        char name[8] = "/dev/sdX";
        name[7] = 'a' + i;
        
        shell_print("  ");
        shell_print(name);
        shell_print(" - ");
        shell_print_num(d->total_sectors * 512 / (1024*1024));
        shell_print(" MB  ");
        shell_print(d->model);
        shell_print("\n");
    }
    
    shell_print("\nSelect disk to install to (a-d): ");
    
    char c = 0;
    while (!keyboard_data_ready());
    c = keyboard_getc();
    shell_print("\n");
    
    int disk = c - 'a';
    if (disk < 0 || disk > 3) {
        shell_print("Invalid disk\n");
        return -1;
    }
    
    disk_info_t* d = udisk_get_info(disk);
    if (!d || !d->present) {
        shell_print("Disk not found\n");
        return -1;
    }
    
    char devname[16] = "/dev/sdX";
    devname[7] = 'a' + disk;
    
    shell_print("Selected: ");
    shell_print(devname);
    shell_print("\n");
    
    if (!ufs_ismounted()) {
        int found = 0;
        for (int i = 0; i < d->partition_count; i++) {
            if (d->partitions[i].present && d->partitions[i].type == PARTITION_UFS) {
                found = 1;
                char pname[16];
                strcpy(pname, devname);
                if (d->partitions[i].partition_num < 10) {
                    pname[8] = '0' + d->partitions[i].partition_num;
                    pname[9] = '\0';
                }
                shell_print("Found UFS partition: ");
                shell_print(pname);
                shell_print("\n");
                shell_print("Mount? (y/n): ");
                
                while (!keyboard_data_ready());
                c = keyboard_getc();
                shell_print("\n");
                
                if (c == 'y' || c == 'Y') {
                    if (ufs_mount(d->partitions[i].start_lba, disk) != 0) {
                        shell_print("Mount failed\n");
                        return -1;
                    }
                }
                break;
            }
        }
        
        if (!found) {
            shell_print("No UFS partition found. Create? (y/n): ");
            while (!keyboard_data_ready());
            c = keyboard_getc();
            shell_print("\n");
            
            if (c == 'y' || c == 'Y') {
                shell_print("Size in MB: ");
                char size_str[16];
                int pos = 0;
                while (1) {
                    while (!keyboard_data_ready());
                    c = keyboard_getc();
                    if (c == '\n') break;
                    if (c >= '0' && c <= '9' && pos < 15) {
                        size_str[pos++] = c;
                        char str[2] = {c, '\0'};
                        shell_print(str);
                    }
                }
                size_str[pos] = '\0';
                shell_print("\n");
                
                u32 size_mb = 0;
                for (int i = 0; i < pos; i++) {
                    size_mb = size_mb * 10 + (size_str[i] - '0');
                }
                
                shell_print("Creating partition... ");
                if (udisk_create_partition(devname, size_mb, PARTITION_UFS) != 0) {
                    shell_print("FAILED\n");
                    return -1;
                }
                shell_print("OK\n");
                
                udisk_scan();
                d = udisk_get_info(disk);
                for (int i = 0; i < d->partition_count; i++) {
                    if (d->partitions[i].present && d->partitions[i].type == PARTITION_UFS) {
                        char pname[16];
                        strcpy(pname, devname);
                        if (d->partitions[i].partition_num < 10) {
                            pname[8] = '0' + d->partitions[i].partition_num;
                            pname[9] = '\0';
                        }
                        shell_print("Formatting... ");
                        u32 blocks = (d->partitions[i].end_lba - d->partitions[i].start_lba + 1);
                        if (ufs_format(d->partitions[i].start_lba, blocks, disk) != 0) {
                            shell_print("FAILED\n");
                            return -1;
                        }
                        shell_print("OK\n");
                        
                        if (ufs_mount(d->partitions[i].start_lba, disk) != 0) {
                            shell_print("Mount failed\n");
                            return -1;
                        }
                        break;
                    }
                }
            } else {
                shell_print("Aborted\n");
                return 0;
            }
        }
    }
    
    if (!ufs_ismounted()) {
        shell_print("No filesystem mounted\n");
        return -1;
    }
    
    shell_print("\nTarget: ");
    shell_print(ufs_get_device());
    shell_print("\n");
    shell_print("Continue? (y/n): ");
    
    while (!keyboard_data_ready());
    c = keyboard_getc();
    shell_print("\n\n");
    if (c != 'y' && c != 'Y') {
        shell_print("Aborted.\n");
        return 0;
    }
    
    shell_print("Creating directories...\n");
    create_directories();
    
    shell_print("\nCopying kernel...\n");
    copy_kernel();
    
    shell_print("\nCopying modules...\n");
    copy_modules();
    
    shell_print("\nConfiguring boot...\n");
    create_grub_cfg();
    
    shell_print("\nDone. System installed to ");
    shell_print(ufs_get_device());
    shell_print("\nYou can now reboot.\n");
    
    return 0;
}
