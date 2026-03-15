#include "../include/string.h"
#include "../drivers/vga.h"
#include "../drivers/disk.h"
#include "../drivers/keyboard.h"
#include "../fs/ufs.h"
#include "../include/part.h"
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
}

static void copy_file(const char* src, const char* dst) {
    u8* data;
    u32 size;
    if (ufs_read(src, &data, &size) != 0) return;
    ufs_write(dst, data, size);
    kfree(data);
}

static void copy_kernel(void) {
    copy_file("/boot/kernel.bin", "/boot/kernel.bin");
}

static void copy_modules(void) {
    FSNode* entries;
    u32 count;
    if (ufs_readdir("/modules", &entries, &count) != 0) return;
    
    for (u32 i = 0; i < count; i++) {
        if (entries[i].is_dir) continue;
        char src[256], dst[256];
        snprintf(src, sizeof(src), "/modules/%s", entries[i].name);
        snprintf(dst, sizeof(dst), "/modules/%s", entries[i].name);
        copy_file(src, dst);
    }
    kfree(entries);
}

static void create_grub_cfg(void) {
    char* cfg = "set timeout=5\n"
                "set default=0\n"
                "menuentry \"UTMS\" {\n"
                "    multiboot2 /boot/kernel.bin\n"
                "    boot\n"
                "}\n";
    ufs_write("/boot/grub/grub.cfg", (u8*)cfg, strlen(cfg));
}

int install_main(int argc, char** argv) {
    (void)argc; (void)argv;
    
    shell_print("\nUTMS Installer\n");
    shell_print("==============\n\n");
    
    if (!ufs_ismounted()) {
        shell_print("ERROR: No partition mounted\n");
        shell_print("1. part create /dev/sdX <size>\n");
        shell_print("2. mkfs.ufs /dev/sdX1\n");
        shell_print("3. mount /dev/sdX1\n");
        return -1;
    }
    
    shell_print("Target: ");
    shell_print(ufs_get_device());
    shell_print("\n\n");
    shell_print("Continue? (y/n): ");
    
    while (!keyboard_data_ready());
    char c = keyboard_getc();
    shell_print("\n\n");
    if (c != 'y' && c != 'Y') return 0;
    
    shell_print("Creating directories...\n");
    create_directories();
    
    shell_print("Copying kernel...\n");
    copy_kernel();
    
    shell_print("Copying modules...\n");
    copy_modules();
    
    shell_print("Configuring boot...\n");
    create_grub_cfg();
    
    shell_print("\nDone. Installed to ");
    shell_print(ufs_get_device());
    shell_print("\n");
    
    return 0;
}
