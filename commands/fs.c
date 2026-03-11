#include "../include/shell_api.h"
#include "../include/string.h"
#include "../fs/ufs.h"
#include "../drivers/vga.h"
#include "../kernel/memory.h"
#include "../drivers/disk.h"

static char current_dir[256] = "/";

static void normalize_path(const char* input, char* output) {
    char temp[256];
    strcpy(temp, input);
    
    char* parts[64];
    int part_count = 0;
    
    char* token = strtok(temp, "/");
    while (token) {
        if (strcmp(token, "..") == 0) {
            if (part_count > 0) part_count--;
        } else if (strcmp(token, ".") != 0 && token[0] != '\0') {
            parts[part_count++] = token;
        }
        token = strtok(NULL, "/");
    }
    
    if (part_count == 0) {
        strcpy(output, "/");
        return;
    }
    
    output[0] = '\0';
    for (int i = 0; i < part_count; i++) {
        strcat(output, "/");
        strcat(output, parts[i]);
    }
}

static void build_path(const char* arg, char* result) {
    char temp[256];
    
    if (arg[0] == '/') {
        strcpy(temp, arg);
    } else if (strcmp(current_dir, "/") == 0) {
        snprintf(temp, sizeof(temp), "/%s", arg);
    } else {
        snprintf(temp, sizeof(temp), "%s/%s", current_dir, arg);
    }
    
    normalize_path(temp, result);
}

static int cmd_ls(int argc, char** argv) {
    char path[256];
    
    if (argc > 1) {
        build_path(argv[1], path);
    } else {
        strcpy(path, current_dir);
    }
    
    FSNode *entries;
    u32 count;
    
    if (ufs_readdir(path, &entries, &count) != 0) {
        shell_print("Failed to list directory\n");
        return -1;
    }
    
    shell_print(path);
    shell_print("\n");
    
    for (u32 i = 0; i < count; i++) {
        // ВРЕМЕННО: показываем имена как есть
        shell_print("  ");
        if (entries[i].is_dir) shell_print("dir  ");
        else shell_print("file ");
        
        shell_print(entries[i].name);
        
        // Выравнивание
        int len = strlen(entries[i].name);
        for (int j = len; j < 20; j++) shell_print(" ");
        
        shell_print_num(entries[i].size);
        shell_print(" bytes\n");
    }
    
    shell_print("\nTotal: ");
    shell_print_num(count);
    shell_print(" entries\n");
    
    if (entries) kfree(entries);
    return 0;
}

static int cmd_cd(int argc, char** argv) {
    if (argc < 2) {
        strcpy(current_dir, "/");
        return 0;
    }
    
    char path[256];
    build_path(argv[1], path);
    
    if (ufs_exists(path) && ufs_isdir(path)) {
        strcpy(current_dir, path);
    } else {
        shell_print("Directory not found\n");
        return -1;
    }
    return 0;
}

static int cmd_pwd(int argc, char** argv) {
    (void)argc; (void)argv;
    shell_print(current_dir);
    shell_print("\n");
    return 0;
}

static int cmd_mkdir(int argc, char** argv) {
    if (argc < 2) {
        shell_print("Usage: mkdir <dir>\n");
        return -1;
    }
    
    char path[256];
    build_path(argv[1], path);
    
    if (ufs_mkdir(path) == 0) {
        shell_print("Directory created\n");
        return 0;
    } else {
        shell_print("Creation failed\n");
        return -1;
    }
}

static int cmd_touch(int argc, char** argv) {
    if (argc < 2) {
        shell_print("Usage: touch <file>\n");
        return -1;
    }
    
    char path[256];
    build_path(argv[1], path);
    
    if (ufs_write(path, NULL, 0) == 0) {
        shell_print("File created\n");
        return 0;
    } else {
        shell_print("Creation failed\n");
        return -1;
    }
}

static int cmd_cat(int argc, char** argv) {
    if (argc < 2) {
        shell_print("Usage: cat <file>\n");
        return -1;
    }
    
    char path[256];
    build_path(argv[1], path);
    
    u8 *data;
    u32 size;
    
    if (ufs_read(path, &data, &size) != 0) {
        shell_print("File not found\n");
        return -1;
    }
    
    shell_print((char*)data);
    if (size > 0 && data[size-1] != '\n') {
        shell_print("\n");
    }
    
    kfree(data);
    return 0;
}

static int cmd_rm(int argc, char** argv) {
    if (argc < 2) {
        shell_print("Usage: rm <file>\n");
        return -1;
    }
    
    char path[256];
    build_path(argv[1], path);
    
    if (ufs_delete(path) == 0) {
        shell_print("File deleted\n");
        return 0;
    } else {
        shell_print("Delete failed\n");
        return -1;
    }
}

static int cmd_rmdir(int argc, char** argv) {
    if (argc < 2) {
        shell_print("Usage: rmdir <dir>\n");
        return -1;
    }
    
    char path[256];
    build_path(argv[1], path);
    
    if (ufs_rmdir(path) == 0) {
        shell_print("Directory removed\n");
        return 0;
    } else {
        shell_print("Remove failed (directory not empty or not found)\n");
        return -1;
    }
}

static int cmd_cp(int argc, char** argv) {
    if (argc < 3) {
        shell_print("Usage: cp <source> <dest>\n");
        return -1;
    }
    
    char src[256], dst[256];
    build_path(argv[1], src);
    build_path(argv[2], dst);
    
    if (ufs_cp(src, dst) == 0) {
        shell_print("Copied\n");
        return 0;
    } else {
        shell_print("Copy failed\n");
        return -1;
    }
}

static int cmd_mv(int argc, char** argv) {
    if (argc < 3) {
        shell_print("Usage: mv <source> <dest>\n");
        return -1;
    }
    
    char src[256], dst[256];
    build_path(argv[1], src);
    build_path(argv[2], dst);
    
    if (ufs_mv(src, dst) == 0) {
        shell_print("Moved\n");
        return 0;
    } else {
        shell_print("Move failed\n");
        return -1;
    }
}

static int cmd_df(int argc, char** argv) {
    (void)argc; (void)argv;
    
    u32 total, used, free;
    if (ufs_stat(&total, &used, &free) != 0) {
        shell_print("Failed to get filesystem stats\n");
        return -1;
    }
    
    shell_print("Filesystem      Size  Used  Free  Use%\n");
    shell_print("UFS             ");
    shell_print_num(total / (1024*1024));
    shell_print("M ");
    shell_print_num(used / (1024*1024));
    shell_print("M ");
    shell_print_num(free / (1024*1024));
    shell_print("M ");
    shell_print_num(used * 100 / total);
    shell_print("%\n");
    
    return 0;
}

static int cmd_mkfs_ufs(int argc, char** argv) {
    u32 start_lba = 0;
    u32 total_blocks = 0;
    int disk_idx = 0;
    
    if (argc < 2) {
        shell_print("Usage: mkfs.ufs <device> [blocks]\n");
        shell_print("Devices: /dev/sda, /dev/sdb, /dev/sdc, /dev/sdd\n");
        return -1;
    }
    
    if (argv[1][0] == '/' && argv[1][1] == 'd' && argv[1][2] == 'e' && argv[1][3] == 'v' && argv[1][4] == '/') {
        if (argv[1][5] == 's' && argv[1][6] == 'd' && argv[1][7] >= 'a' && argv[1][7] <= 'd') {
            disk_idx = argv[1][7] - 'a';
            disk_set_disk(disk_idx);
            
            u64 sectors = disk_get_sectors(0x80 + disk_idx);
            if (sectors == 0) {
                sectors = 5120 * 1024 * 1024 / 512;
            }
            
            start_lba = 2048;
            total_blocks = sectors - 2048;
        }
    }
    
    if (argc >= 3) {
        char* end;
        total_blocks = 0;
        for (int i = 0; argv[2][i]; i++) {
            if (argv[2][i] >= '0' && argv[2][i] <= '9') {
                total_blocks = total_blocks * 10 + (argv[2][i] - '0');
            }
        }
    }
    
    if (total_blocks == 0) {
        shell_print("Invalid device or size\n");
        return -1;
    }
    
    shell_print("Creating UFS on ");
    shell_print(argv[1]);
    shell_print(" (");
    shell_print_num(total_blocks * 512 / (1024*1024));
    shell_print(" MB)... ");
    
    if (ufs_format(start_lba, total_blocks) == 0) {
        shell_print("OK\n");
    } else {
        shell_print("FAILED\n");
    }
    
    return 0;
}

static int cmd_mount(int argc, char** argv) {
    u32 start_lba = 2048;
    
    if (argc > 1) {
        if (argv[1][0] == '/' && argv[1][1] == 'd' && argv[1][2] == 'e' && argv[1][3] == 'v' && argv[1][4] == '/') {
            if (argv[1][5] == 's' && argv[1][6] == 'd') {
                int idx = argv[1][7] - 'a';
                disk_set_disk(idx);
            }
        }
    }
    
    if (ufs_mount(start_lba) == 0) {
        shell_print("Mounted\n");
        strcpy(current_dir, "/");
    } else {
        shell_print("Mount failed\n");
    }
    
    return 0;
}

static int cmd_umount(int argc, char** argv) {
    (void)argc; (void)argv;
    
    // Пока ничего не делаем, UFS всегда смонтирована
    shell_print("UFS always mounted\n");
    return 0;
}

static int cmd_chmod(int argc, char** argv) {
    if (argc < 3) {
        shell_print("Usage: chmod <mode> <file>\n");
        return -1;
    }
    
    char* end;
    u16 mode = 0;
    for (int i = 0; argv[1][i]; i++) {
        if (argv[1][i] >= '0' && argv[1][i] <= '7') {
            mode = mode * 8 + (argv[1][i] - '0');
        }
    }
    
    char path[256];
    build_path(argv[2], path);
    
    if (ufs_chmod(path, mode) == 0) {
        shell_print("OK\n");
        return 0;
    } else {
        shell_print("Failed\n");
        return -1;
    }
}

static int cmd_chown(int argc, char** argv) {
    if (argc < 4) {
        shell_print("Usage: chown <uid> <gid> <file>\n");
        return -1;
    }
    
    char* end;
    u16 uid = 0, gid = 0;
    for (int i = 0; argv[1][i]; i++) {
        if (argv[1][i] >= '0' && argv[1][i] <= '9') {
            uid = uid * 10 + (argv[1][i] - '0');
        }
    }
    for (int i = 0; argv[2][i]; i++) {
        if (argv[2][i] >= '0' && argv[2][i] <= '9') {
            gid = gid * 10 + (argv[2][i] - '0');
        }
    }
    
    char path[256];
    build_path(argv[3], path);
    
    if (ufs_chown(path, uid, gid) == 0) {
        shell_print("OK\n");
        return 0;
    } else {
        shell_print("Failed\n");
        return -1;
    }
}

static void find_callback(const char* path) {
    shell_print(path);
    shell_print("\n");
}

static int cmd_find(int argc, char** argv) {
    if (argc < 2) {
        shell_print("Usage: find <name> [start_path]\n");
        return -1;
    }
    
    char start[256] = "/";
    if (argc > 2) {
        build_path(argv[2], start);
    }
    
    shell_print("Searching for '");
    shell_print(argv[1]);
    shell_print("' in ");
    shell_print(start);
    shell_print("\n");
    
    ufs_find(start, argv[1], find_callback);
    
    return 0;
}

static void grep_callback(const char* line, u32 line_num) {
    shell_print("Line ");
    shell_print_num(line_num);
    shell_print(": ");
    shell_print(line);
    shell_print("\n");
}

static int cmd_grep(int argc, char** argv) {
    if (argc < 3) {
        shell_print("Usage: grep <pattern> <file>\n");
        return -1;
    }
    
    char path[256];
    build_path(argv[2], path);
    
    return ufs_grep(path, argv[1], grep_callback);
}

void fs_commands_init(void) {
    shell_register_command("ls", cmd_ls, "List directory");
    shell_register_command("cd", cmd_cd, "Change directory");
    shell_register_command("pwd", cmd_pwd, "Print working directory");
    shell_register_command("mkdir", cmd_mkdir, "Create directory");
    shell_register_command("rmdir", cmd_rmdir, "Remove directory");
    shell_register_command("touch", cmd_touch, "Create empty file");
    shell_register_command("cat", cmd_cat, "Show file content");
    shell_register_command("rm", cmd_rm, "Delete file");
    shell_register_command("cp", cmd_cp, "Copy file");
    shell_register_command("mv", cmd_mv, "Move file");
    shell_register_command("df", cmd_df, "Show filesystem usage");
    shell_register_command("mkfs.ufs", cmd_mkfs_ufs, "Create UFS filesystem");
    shell_register_command("mount", cmd_mount, "Mount UFS");
    shell_register_command("umount", cmd_umount, "Unmount UFS");
    shell_register_command("chmod", cmd_chmod, "Change file mode");
    shell_register_command("chown", cmd_chown, "Change file owner");
    shell_register_command("find", cmd_find, "Find files");
    shell_register_command("grep", cmd_grep, "Search in file");
}
