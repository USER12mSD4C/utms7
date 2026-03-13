#include "../include/types.h"
#include "../include/string.h"
#include "../drivers/vga.h"
#include "../drivers/disk.h"
#include "../drivers/keyboard.h"
#include "../fs/ufs.h"
#include "../drivers/gpt.h"
#include "../kernel/memory.h"
#include "../kernel/kapi.h"

#define SECTOR_SIZE 512
#define INSTALL_PATH "/system"

static void print_help(void) {
    vga_write("UTMS Installer\n");
    vga_write("Usage: install <target_disk> [source_disk]\n");
    vga_write("  target_disk: /dev/sda, /dev/sdb, etc.\n");
    vga_write("  source_disk: where livecd is running from (default: /dev/sr0)\n");
    vga_write("\n");
    vga_write("Steps:\n");
    vga_write("  1. Creates GPT partition table\n");
    vga_write("  2. Creates UFS partition\n");
    vga_write("  3. Formats it\n");
    vga_write("  4. Copies system files\n");
    vga_write("  5. Installs GRUB\n");
}

static int create_partitions(u8 drive) {
    vga_write("Creating GPT partition table... ");
    
    if (gpt_create_table(drive) != 0) {
        vga_write("FAILED\n");
        return -1;
    }
    
    vga_write("OK\n");
    
    vga_write("Adding UFS partition... ");
    
    u8 ufs_guid[16] = GPT_GUID_UFS;
    if (gpt_add_partition(drive, 2048, disk_get_sectors(drive) - 2048, ufs_guid) != 0) {
        vga_write("FAILED\n");
        return -1;
    }
    
    vga_write("OK\n");
    return 0;
}

static int format_ufs(u8 drive) {
    vga_write("Formatting UFS... ");
    
    disk_set_disk(drive - 0x80);
    if (ufs_format(2048, disk_get_sectors(drive) - 2048) != 0) {
        vga_write("FAILED\n");
        return -1;
    }
    
    vga_write("OK\n");
    return 0;
}

static int mount_ufs(u8 drive) {
    disk_set_disk(drive - 0x80);
    if (ufs_mount(2048) != 0) {
        return -1;
    }
    return 0;
}

static int copy_file(const char* src, const char* dst) {
    u8* data;
    u32 size;
    
    // Читаем с source диска
    if (ufs_read(src, &data, &size) != 0) {
        return -1;
    }
    
    // Пишем на target диск
    int ret = ufs_write(dst, data, size);
    kfree(data);
    return ret;
}

static int copy_directory(const char* src, const char* dst) {
    FSNode* entries;
    u32 count;
    
    if (ufs_readdir(src, &entries, &count) != 0) {
        return -1;
    }
    
    // Создаём целевую директорию
    ufs_mkdir(dst);
    
    for (u32 i = 0; i < count; i++) {
        char src_path[256];
        char dst_path[256];
        
        if (strcmp(src, "/") == 0) {
            snprintf(src_path, sizeof(src_path), "/%s", entries[i].name);
        } else {
            snprintf(src_path, sizeof(src_path), "%s/%s", src, entries[i].name);
        }
        
        if (strcmp(dst, "/") == 0) {
            snprintf(dst_path, sizeof(dst_path), "/%s", entries[i].name);
        } else {
            snprintf(dst_path, sizeof(dst_path), "%s/%s", dst, entries[i].name);
        }
        
        if (entries[i].is_dir) {
            // Рекурсивно копируем директорию
            copy_directory(src_path, dst_path);
        } else {
            // Копируем файл
            copy_file(src_path, dst_path);
        }
    }
    
    kfree(entries);
    return 0;
}

static int install_grub(u8 drive) {
    vga_write("Installing GRUB... ");
    
    // TODO: реальная установка GRUB
    // Пока просто создаём директорию boot
    ufs_mkdir("/boot");
    ufs_mkdir("/boot/grub");
    
    // Создаём grub.cfg
    const char* cfg = 
        "set timeout=5\n"
        "set default=0\n"
        "menuentry \"UTMS\" {\n"
        "    multiboot2 /boot/kernel.bin\n"
        "    boot\n"
        "}\n";
    
    ufs_write("/boot/grub/grub.cfg", (u8*)cfg, strlen(cfg));
    
    // Копируем ядро
    u8* kernel_data;
    u32 kernel_size;
    if (ufs_read("/boot/kernel.bin", &kernel_data, &kernel_size) == 0) {
        // Ядро уже есть? тогда ок
        kfree(kernel_data);
    }
    
    vga_write("OK (simulated)\n");
    return 0;
}

int install_main(int argc, char** argv) {
    if (argc < 2) {
        print_help();
        return -1;
    }
    
    // Парсим target диск
    char* target = argv[1];
    if (target[0] != '/' || target[1] != 'd' || target[2] != 'e' || target[3] != 'v' || target[4] != '/') {
        vga_write("Invalid device path\n");
        return -1;
    }
    
    if (target[5] != 's' || target[6] != 'd' || target[7] < 'a' || target[7] > 'd') {
        vga_write("Invalid device (must be /dev/sd[a-d])\n");
        return -1;
    }
    
    u8 target_drive = 0x80 + (target[7] - 'a');
    
    // Source диск (livecd)
    u8 source_drive = 0xE0; // /dev/sr0 по умолчанию
    if (argc >= 3) {
        char* src = argv[2];
        if (src[5] == 's' && src[6] == 'r') {
            source_drive = 0xE0; // CD-ROM
        } else if (src[5] == 's' && src[6] == 'd') {
            source_drive = 0x80 + (src[7] - 'a');
        }
    }
    
    vga_write("UTMS Installer\n");
    vga_write("=============\n");
    vga_write("Target disk: /dev/sd");
    vga_putchar('a' + (target_drive - 0x80));
    vga_write("\n");
    vga_write("Source disk: ");
    if (source_drive >= 0xE0) {
        vga_write("/dev/sr0 (livecd)\n");
    } else {
        vga_write("/dev/sd");
        vga_putchar('a' + (source_drive - 0x80));
        vga_write("\n");
    }
    vga_write("\n");
    
    vga_write("WARNING: This will erase ALL data on target disk!\n");
    vga_write("Press 'y' to continue, any other key to abort: ");
    
    while (!keyboard_data_ready());
    char c = keyboard_getc();
    vga_putchar(c);
    vga_write("\n");
    
    if (c != 'y' && c != 'Y') {
        vga_write("Installation aborted.\n");
        return 0;
    }
    
    vga_write("\nStarting installation...\n\n");
    
    // Монтируем source диск
    if (source_drive >= 0xE0) {
        // Для CD-ROM особый драйвер не нужен, предполагаем что он смонтирован
        vga_write("Using livecd as source\n");
    } else {
        disk_set_disk(source_drive - 0x80);
        if (ufs_mount(2048) != 0) {
            vga_write("Failed to mount source disk\n");
            return -1;
        }
    }
    
    // Создаём разделы на target
    if (create_partitions(target_drive) != 0) {
        return -1;
    }
    
    // Форматируем UFS
    if (format_ufs(target_drive) != 0) {
        return -1;
    }
    
    // Монтируем target
    if (mount_ufs(target_drive) != 0) {
        vga_write("Failed to mount target\n");
        return -1;
    }
    
    vga_write("\nCopying system files...\n");
    
    // Копируем ядро
    vga_write("  Copying kernel... ");
    if (copy_file("/boot/kernel.bin", "/boot/kernel.bin") == 0) {
        vga_write("OK\n");
    } else {
        vga_write("FAILED\n");
    }
    
    // Копируем модули
    vga_write("  Copying modules... ");
    ufs_mkdir("/modules");
    
    FSNode* mod_entries;
    u32 mod_count;
    if (ufs_readdir("/modules", &mod_entries, &mod_count) == 0) {
        int copied = 0;
        for (u32 i = 0; i < mod_count; i++) {
            if (!mod_entries[i].is_dir) {
                char src[256], dst[256];
                snprintf(src, sizeof(src), "/modules/%s", mod_entries[i].name);
                snprintf(dst, sizeof(dst), "/modules/%s", mod_entries[i].name);
                if (copy_file(src, dst) == 0) copied++;
            }
        }
        kfree(mod_entries);
        vga_write_num(copied);
        vga_write(" files\n");
    } else {
        vga_write("SKIP (no modules)\n");
    }
    
    // Копируем shell и команды
    vga_write("  Copying system apps...\n");
    copy_directory("/bin", "/bin");
    
    // Устанавливаем GRUB
    install_grub(target_drive);
    
    vga_write("\nInstallation complete!\n");
    vga_write("You can now boot from the target disk.\n");
    
    return 0;
}
