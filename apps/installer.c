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
    ufs_mkdir("/etc/upac");
    ufs_mkdir("/etc/upac/installed");
    ufs_mkdir("/home");
    ufs_mkdir("/usr");
    ufs_mkdir("/usr/bin");
    ufs_mkdir("/usr/lib");
    ufs_mkdir("/usr/share");
    ufs_mkdir("/docs");
    ufs_mkdir("/var");
    ufs_mkdir("/var/log");
    ufs_mkdir("/var/tmp");
    ufs_mkdir("/tmp");
    ufs_mkdir("/dev");
    ufs_mkdir("/proc");
    ufs_mkdir("/sys");
    ufs_mkdir("/mnt");
}

static void copy_file(const char* src, const char* dst) {
    u8* data;
    u32 size;
    
    if (ufs_read(src, &data, &size) != 0) {
        shell_print("  missing: ");
        shell_print(src);
        shell_print("\n");
        return;
    }
    
    if (ufs_write(dst, data, size) == 0) {
        shell_print("  ");
        shell_print(dst);
        shell_print("\n");
    } else {
        shell_print("  FAILED: ");
        shell_print(dst);
        shell_print("\n");
    }
    kfree(data);
}

static void copy_kernel(void) {
    shell_print("  Copying kernel...\n");
    
    if (ufs_exists("/mnt/livecd/install/kernel.bin")) {
        copy_file("/mnt/livecd/install/kernel.bin", "/boot/kernel.bin");
    } else {
        shell_print("  kernel.bin not found in /mnt/livecd/install\n");
    }
}

static void copy_modules(void) {
    shell_print("  Copying modules...\n");
    
    FSNode* entries;
    u32 count;
    
    if (ufs_readdir("/mnt/livecd/install/modules", &entries, &count) != 0) {
        shell_print("  no modules found in /mnt/livecd/install/modules\n");
        return;
    }
    
    int copied = 0;
    for (u32 i = 0; i < count; i++) {
        if (entries[i].is_dir) continue;
        
        int len = strlen(entries[i].name);
        if (len < 3 || strcmp(entries[i].name + len - 3, ".ko") != 0) continue;
        
        char src[256];
        char dst[256];
        snprintf(src, sizeof(src), "/mnt/livecd/install/modules/%s", entries[i].name);
        snprintf(dst, sizeof(dst), "/modules/%s", entries[i].name);
        
        copy_file(src, dst);
        copied++;
    }
    kfree(entries);
    
    shell_print("  copied ");
    shell_print_num(copied);
    shell_print(" modules\n");
}

static void create_grub_cfg(void) {
    char* cfg = 
        "set timeout=5\n"
        "set default=0\n"
        "menuentry \"UTMS\" {\n"
        "    multiboot2 /boot/kernel.bin\n"
        "    boot\n"
        "}\n";
    
    if (ufs_write("/boot/grub/grub.cfg", (u8*)cfg, strlen(cfg)) == 0) {
        shell_print("  /boot/grub/grub.cfg\n");
    } else {
        shell_print("  FAILED: grub.cfg\n");
    }
}

int install_main(int argc, char** argv) {
    (void)argc; (void)argv;
    
    shell_print("\nUTMS Installer\n");
    shell_print("==============\n\n");
    
    // Проверяем что LiveCD смонтирован
    if (!ufs_isdir("/mnt/livecd")) {
        shell_print("ERROR: LiveCD not mounted at /mnt/livecd\n");
        return -1;
    }
    
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
        shell_print(" MB");
        if (d->is_gpt) shell_print(" GPT");
        else shell_print(" MBR");
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
    
    // Ищем UFS раздел
    int found = 0;
    u32 start_lba = 2048;
    int part_num = 1;
    
    for (int i = 0; i < d->partition_count; i++) {
        if (d->partitions[i].present && d->partitions[i].type == PARTITION_UFS) {
            start_lba = d->partitions[i].start_lba;
            part_num = d->partitions[i].partition_num;
            found = 1;
            break;
        }
    }
    
    if (!found) {
        shell_print("No UFS partition found. Create one first.\n");
        return -1;
    }
    
    char partname[16];
    snprintf(partname, sizeof(partname), "/dev/sd%c%d", 'a' + disk, part_num);
    shell_print("Target partition: ");
    shell_print(partname);
    shell_print("\n");
    
    shell_print("Continue? (y/n): ");
    while (!keyboard_data_ready());
    c = keyboard_getc();
    shell_print("\n\n");
    if (c != 'y' && c != 'Y') {
        shell_print("Aborted.\n");
        return 0;
    }
    
    if (ufs_mount_with_point(start_lba, disk, "/mnt") != 0) {
        shell_print("Mount failed\n");
        return -1;
    }
    shell_print("Target mounted at /mnt\n");
    
    shell_print("Creating directories...\n");
    create_directories();
    
    shell_print("\nCopying kernel...\n");
    copy_kernel();
    
    shell_print("\nCopying modules...\n");
    copy_modules();
    
    shell_print("\nConfiguring boot...\n");
    create_grub_cfg();
    
    shell_print("\nUnmounting target... ");
    ufs_umount();
    shell_print("OK\n");
    
    shell_print("\n====================================\n");
    shell_print("Installation complete!\n");
    shell_print("====================================\n");
    shell_print("\nYou can now reboot.\n");
    
    return 0;
}
