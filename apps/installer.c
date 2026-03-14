#include "../include/types.h"
#include "../include/string.h"
#include "../drivers/vga.h"
#include "../drivers/disk.h"
#include "../drivers/keyboard.h"    // ЭТО ДОБАВИТЬ
#include "../fs/ufs.h"
#include "../kernel/memory.h"       // ЭТО ДОБАВИТЬ (для kfree)
#include "../kernel/kapi.h"

static void create_directories(void) {
    vga_write("Creating directories...\n");
    
    ufs_mkdir("/boot");
    ufs_mkdir("/modules");
    ufs_mkdir("/bin");
    ufs_mkdir("/docs");
    ufs_mkdir("/etc");
    
    vga_write("  /boot\n");
    vga_write("  /modules\n");
    vga_write("  /bin\n");
    vga_write("  /docs\n");
    vga_write("  /etc\n");
}

static void copy_modules(void) {
    vga_write("Copying modules...\n");
    
    // Читаем список модулей из livecd
    FSNode* entries;
    u32 count;
    
    if (ufs_readdir("/modules", &entries, &count) != 0) {
        vga_write("  No modules found\n");
        return;
    }
    
    int copied = 0;
    for (u32 i = 0; i < count; i++) {
        if (!entries[i].is_dir) {
            char src[256];
            char dst[256];
            
            strcpy(src, "/modules/");
            strcat(src, entries[i].name);
            
            strcpy(dst, "/modules/");
            strcat(dst, entries[i].name);
            
            u8* data;
            u32 size;
            if (ufs_read(src, &data, &size) == 0) {
                if (ufs_write(dst, data, size) == 0) {
                    copied++;
                }
                kfree(data);
            }
        }
    }
    
    kfree(entries);
    vga_write("  Copied ");
    vga_write_num(copied);
    vga_write(" modules\n");
}

static void copy_kernel(void) {
    vga_write("Copying kernel...\n");
    
    u8* data;
    u32 size;
    
    if (ufs_read("/boot/kernel.bin", &data, &size) == 0) {
        if (ufs_write("/boot/kernel.bin", data, size) == 0) {
            vga_write("  Kernel copied\n");
        }
        kfree(data);
    } else {
        vga_write("  Kernel not found in livecd\n");
    }
}

int install_auto_main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    
    vga_write("\nUTMS Auto Installer\n");
    vga_write("==================\n\n");
    
    // Определяем целевой диск
    int target_disk = 0; // /dev/sda
    
    // Ищем первый доступный диск
    for (int i = 0; i < 4; i++) {
        if (disk_get_sectors(0x80 + i) > 0) {
            target_disk = i;
            break;
        }
    }
    
    vga_write("Target disk: /dev/sd");
    vga_putchar('a' + target_disk);
    vga_write("\n\n");
    
    vga_write("WARNING: This will erase ALL data on disk!\n");
    vga_write("Press 'y' to continue: ");
    
    // Ждём подтверждения
    while (!keyboard_data_ready());
    char c = keyboard_getc();
    vga_putchar(c);
    vga_write("\n\n");
    
    if (c != 'y' && c != 'Y') {
        vga_write("Installation aborted.\n");
        return 0;
    }
    
    // Устанавливаем целевой диск
    disk_set_disk(target_disk);
    
    // Форматируем в UFS
    vga_write("Formatting UFS... ");
    u64 sectors = disk_get_sectors(0x80 + target_disk);
    if (ufs_format(2048, sectors - 2048) != 0) {
        vga_write("FAILED\n");
        return -1;
    }
    vga_write("OK\n");
    
    // Монтируем
    if (ufs_mount(2048) != 0) {
        vga_write("Mount failed\n");
        return -1;
    }
    
    // Создаём директории
    create_directories();
    
    // Копируем модули (из livecd /modules в целевой /modules)
    copy_modules();
    
    // Копируем ядро
    copy_kernel();
    
    vga_write("\nInstallation complete!\n");
    vga_write("You can now reboot and boot from this disk.\n");
    
    return 0;
}
