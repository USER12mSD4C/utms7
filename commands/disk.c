#include "../include/shell_api.h"
#include "../include/string.h"
#include "../drivers/disk.h"
#include "../drivers/vga.h"

static int cmd_disks(int argc, char** argv) {
    (void)argc; (void)argv;
    disk_list_disks();
    return 0;
}

static int cmd_disk_info(int argc, char** argv) {
    if (argc < 2) {
        shell_print("Usage: diskinfo <disk>\n");
        return -1;
    }
    
    u8 drive = 0x80; // TODO: парсить аргумент
    u64 sectors = disk_get_sectors(drive);
    
    shell_print("Disk sectors: ");
    shell_print_num(sectors);
    shell_print("\n");
    return 0;
}

void disk_commands_init(void) {
    shell_register_command("disks", cmd_disks, "List all disks");
    shell_register_command("diskinfo", cmd_disk_info, "Show disk info");
}
