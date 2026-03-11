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
    
    u8 drive = 0x80;
    if (argv[1][0] == 's' && argv[1][1] == 'd') {
        drive = 0x80 + (argv[1][2] - 'a');
    }
    
    u64 sectors = disk_get_sectors(drive);
    
    shell_print("Disk ");
    shell_print(argv[1]);
    shell_print(": ");
    shell_print_num(sectors * 512 / (1024*1024));
    shell_print(" MB (");
    shell_print_num(sectors);
    shell_print(" sectors)\n");
    return 0;
}

static int cmd_lsblk(int argc, char** argv) {
    (void)argc; (void)argv;
    
    shell_print("NAME   SIZE   TYPE\n");
    
    for (int i = 0; i < 4; i++) {
        char name[8] = "sdX";
        name[2] = 'a' + i;
        
        u64 sectors = disk_get_sectors(0x80 + i);
        if (sectors > 0) {
            shell_print(name);
            shell_print("   ");
            shell_print_num(sectors * 512 / (1024*1024));
            shell_print("MB   disk\n");
        }
    }
    
    return 0;
}

void disk_commands_init(void) {
    shell_register_command("disks", cmd_disks, "List all disks");
    shell_register_command("diskinfo", cmd_disk_info, "Show disk info");
    shell_register_command("lsblk", cmd_lsblk, "List block devices");
}
