#include "../include/string.h"
#include "../drivers/drm.h"
#include "../drivers/disk.h"
#include "../drivers/keyboard.h"
#include "../include/udisk.h"
#include "../kernel/memory.h"
#include "../kernel/vfs.h"
#include "../include/shell_api.h"

static void create_directories(void) {
    vfs_mkdir("/boot", 0755);
    vfs_mkdir("/boot/grub", 0755);
    vfs_mkdir("/modules", 0755);
    vfs_mkdir("/bin", 0755);
    vfs_mkdir("/etc", 0755);
    vfs_mkdir("/etc/upac", 0755);
    vfs_mkdir("/etc/upac/installed", 0755);
    vfs_mkdir("/home", 0755);
    vfs_mkdir("/usr", 0755);
    vfs_mkdir("/usr/bin", 0755);
    vfs_mkdir("/usr/lib", 0755);
    vfs_mkdir("/usr/share", 0755);
    vfs_mkdir("/docs", 0755);
    vfs_mkdir("/var", 0755);
    vfs_mkdir("/var/log", 0755);
    vfs_mkdir("/var/tmp", 0755);
    vfs_mkdir("/tmp", 0777);
    vfs_mkdir("/dev", 0755);
    vfs_mkdir("/proc", 0555);
    vfs_mkdir("/sys", 0555);
    vfs_mkdir("/mnt", 0755);
}

static void copy_file(const char* src, const char* dst) {
    u8* data = NULL;
    u32 size = 0;

    if (vfs_read_entire(src, &data, &size) != 0) {
        shell_print("  missing: ");
        shell_print(src);
        shell_print("\n");
        return;
    }

    if (vfs_write_entire(dst, data, size) == 0) {
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

    if (vfs_exists("/mnt/livecd/install/kernel.bin")) {
        copy_file("/mnt/livecd/install/kernel.bin", "/boot/kernel.bin");
    } else {
        shell_print("  kernel.bin not found\n");
    }
}

static void copy_modules(void) {
    shell_print("  Copying modules...\n");

    vfs_node_t* dir = vfs_resolve_path("/mnt/livecd/install/modules");
    if (!dir || dir->type != VFS_DIR) {
        shell_print("  no modules found\n");
        return;
    }

    vfs_dirent_t entries[64];
    u32 count = 64;
    if (vfs_readdir(dir, entries, &count) != 0) {
        shell_print("  cannot read modules\n");
        return;
    }

    int copied = 0;
    for (u32 i = 0; i < count; i++) {
        if (entries[i].type == VFS_DIR) continue;
        int len = strlen(entries[i].name);
        if (len < 3 || strcmp(entries[i].name + len - 3, ".ko") != 0) continue;

        char src[256], dst[256];
        snprintf(src, sizeof(src), "/mnt/livecd/install/modules/%s", entries[i].name);
        snprintf(dst, sizeof(dst), "/modules/%s", entries[i].name);

        copy_file(src, dst);
        copied++;
    }

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

    if (vfs_write_entire("/boot/grub/grub.cfg", (u8*)cfg, strlen(cfg)) == 0) {
        shell_print("  /boot/grub/grub.cfg\n");
    } else {
        shell_print("  FAILED: grub.cfg\n");
    }
}

int install_main(int argc, char** argv) {
    (void)argc; (void)argv;

    shell_print("\nUTMS Installer\n");
    shell_print("==============\n\n");

    if (!vfs_isdir("/mnt/livecd")) {
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

    int found = 0;
    int part_num = 1;

    for (int i = 0; i < d->partition_count; i++) {
        if (d->partitions[i].present && d->partitions[i].type == PARTITION_UFS) {
            part_num = d->partitions[i].partition_num;
            found = 1;
            break;
        }
    }

    if (!found) {
        shell_print("No UFS partition found.\n");
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

    if (vfs_mount_fs("ufs", partname, "/mnt") != 0) {
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
    vfs_unmount("/mnt");
    shell_print("OK\n");

    shell_print("\n====================================\n");
    shell_print("Installation complete!\n");
    shell_print("====================================\n");

    return 0;
}
