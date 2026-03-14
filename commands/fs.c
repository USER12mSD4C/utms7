#include "../include/shell_api.h"
#include "../include/string.h"
#include "../fs/ufs.h"
#include "../drivers/vga.h"
#include "../kernel/memory.h"
#include "../drivers/disk.h"

static char current_dir[256] = "/";

// НОРМАЛЬНАЯ normalize_path - копирует строки, а не хранит указатели
static void normalize_path(const char* input, char* output) {
    if (!input || input[0] == '\0') {
        strcpy(output, "/");
        return;
    }
    
    // Временный буфер для разбора
    char temp[256];
    strcpy(temp, input);
    
    // Массив для хранения компонентов пути (максимум 64 уровня)
    char parts[64][256];
    int part_count = 0;
    
    // Разбиваем по '/'
    char* token = strtok(temp, "/");
    while (token && part_count < 64) {
        if (strcmp(token, "..") == 0) {
            // Подняться на уровень вверх
            if (part_count > 0) part_count--;
        } else if (strcmp(token, ".") != 0 && token[0] != '\0') {
            // Нормальный компонент - копируем
            strcpy(parts[part_count], token);
            part_count++;
        }
        token = strtok(NULL, "/");
    }
    
    // Собираем результат
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

// build_path теперь надёжнее
static void build_path(const char* arg, char* result) {
    if (!arg || arg[0] == '\0') {
        strcpy(result, current_dir);
        return;
    }
    
    char temp[512];  // Большой буфер
    
    if (arg[0] == '/') {
        strcpy(temp, arg);
    }
    else if (strcmp(current_dir, "/") == 0) {
        temp[0] = '/';
        temp[1] = '\0';
        strcat(temp, arg);
    }
    else {
        strcpy(temp, current_dir);
        strcat(temp, "/");
        strcat(temp, arg);
    }
    
    normalize_path(temp, result);
}

// ========== LS ==========
static int cmd_ls(int argc, char** argv) {
    char path[256] = "/";
    int show_all = 0;
    
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            for (int j = 1; argv[i][j]; j++) {
                if (argv[i][j] == 'a') show_all = 1;
            }
        } else {
            build_path(argv[i], path);
        }
    }
    
    if (argc <= 1 || (argc == 2 && argv[1][0] == '-')) {
        strcpy(path, current_dir);
    }
    
    FSNode *entries = NULL;
    u32 count = 0;
    
    if (ufs_readdir(path, &entries, &count) != 0) {
        shell_print("ls: cannot access '");
        shell_print(path);
        shell_print("': No such directory\n");
        return -1;
    }
    
    for (u32 i = 0; i < count; i++) {
        if (!show_all && entries[i].name[0] == '.') continue;
        
        if (entries[i].is_dir) shell_print("d ");
        else shell_print("- ");
        
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

// ========== CD ==========
static int cmd_cd(int argc, char** argv) {
    char path[256];
    
    if (argc < 2) {
        strcpy(current_dir, "/");
        return 0;
    }
    
    build_path(argv[1], path);
    
    // Проверяем что это директория
    if (!ufs_isdir(path)) {
        shell_print("cd: not a directory: ");
        shell_print(path);
        shell_print("\n");
        return -1;
    }
    
    strcpy(current_dir, path);
    return 0;
}

// ========== PWD ==========
const char* fs_get_current_dir(void) {
    return current_dir;
}

static int cmd_pwd(int argc, char** argv) {
    (void)argc; (void)argv;
    shell_print(current_dir);
    shell_print("\n");
    return 0;
}

// ========== MKDIR ==========
static int cmd_mkdir(int argc, char** argv) {
    if (argc < 2) {
        shell_print("Usage: mkdir <dir>\n");
        return -1;
    }
    
    char path[256];
    build_path(argv[1], path);
    
    if (ufs_mkdir(path) == 0) {
        return 0;
    } else {
        shell_print("mkdir: failed to create '");
        shell_print(path);
        shell_print("'\n");
        return -1;
    }
}

// ========== RMDIR ==========
static int cmd_rmdir(int argc, char** argv) {
    if (argc < 2) {
        shell_print("Usage: rmdir [-f] <dir>\n");
        return -1;
    }
    
    int force = 0;
    char* target = NULL;
    
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            for (int j = 1; argv[i][j]; j++) {
                if (argv[i][j] == 'f') force = 1;
            }
        } else {
            target = argv[i];
        }
    }
    
    if (!target) {
        shell_print("rmdir: no directory specified\n");
        return -1;
    }
    
    char path[256];
    build_path(target, path);
    
    if (strcmp(path, "/") == 0) {
        shell_print("rmdir: cannot remove root directory\n");
        return -1;
    }
    
    int ret;
    if (force) {
        ret = ufs_rmdir_force(path);
    } else {
        ret = ufs_rmdir(path);
    }
    
    if (ret == 0) {
        return 0;
    } else {
        if (!force) {
            shell_print("rmdir: directory not empty (use -f)\n");
        } else {
            shell_print("rmdir: failed to remove '");
            shell_print(path);
            shell_print("'\n");
        }
        return -1;
    }
}

// ========== MK (create empty file) ==========
static int cmd_mk(int argc, char** argv) {
    if (argc < 2) {
        shell_print("Usage: mk <file>\n");
        return -1;
    }
    
    char path[256];
    build_path(argv[1], path);
    
    if (ufs_exists(path)) {
        shell_print("mk: file already exists\n");
        return -1;
    }
    
    if (ufs_write(path, NULL, 0) == 0) {
        return 0;
    } else {
        shell_print("mk: failed to create file\n");
        return -1;
    }
}

// ========== ECHO ==========
static int cmd_echo(int argc, char** argv) {
    if (argc < 2) {
        shell_print("\n");
        return 0;
    }
    
    int redirect = 0;
    char* file = NULL;
    
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '>' && argv[i][1] == '>') {
            redirect = 2;
            if (i + 1 < argc) file = argv[i + 1];
            argc = i;
            break;
        } else if (argv[i][0] == '>') {
            redirect = 1;
            if (i + 1 < argc) file = argv[i + 1];
            argc = i;
            break;
        }
    }
    
    char output[1024] = {0};
    int pos = 0;
    for (int i = 1; i < argc; i++) {
        char* s = argv[i];
        while (*s) {
            if (pos < 1023) output[pos++] = *s;
            s++;
        }
        if (i < argc - 1 && pos < 1023) output[pos++] = ' ';
    }
    
    if (file) {
        char path[256];
        build_path(file, path);
        
        if (redirect == 2) {
            // Append
            u8* old_data = NULL;
            u32 old_size = 0;
            ufs_read(path, &old_data, &old_size);
            
            u8* new_data = kmalloc(old_size + pos + 2);
            if (old_data && old_size > 0) {
                memcpy(new_data, old_data, old_size);
                kfree(old_data);
            }
            memcpy(new_data + old_size, output, pos);
            new_data[old_size + pos] = '\n';
            ufs_write(path, new_data, old_size + pos + 1);
            kfree(new_data);
        } else {
            // Overwrite
            u8* new_data = kmalloc(pos + 2);
            memcpy(new_data, output, pos);
            new_data[pos] = '\n';
            new_data[pos + 1] = '\0';
            ufs_write(path, new_data, pos + 1);
            kfree(new_data);
        }
    } else {
        output[pos] = '\0';
        shell_print(output);
        shell_print("\n");
    }
    
    return 0;
}

// ========== CAT ==========
static int cmd_cat(int argc, char** argv) {
    if (argc < 2) {
        shell_print("Usage: cat <file>\n");
        return -1;
    }
    
    char path[256];
    build_path(argv[1], path);
    
    u8 *data = NULL;
    u32 size = 0;
    
    if (ufs_read(path, &data, &size) != 0) {
        shell_print("cat: ");
        shell_print(path);
        shell_print(": No such file\n");
        return -1;
    }
    
    if (size > 0) {
        shell_print((char*)data);
        if (data[size-1] != '\n') {
            shell_print("\n");
        }
    }
    
    if (data) kfree(data);
    return 0;
}

// ========== RM ==========
static int cmd_rm(int argc, char** argv) {
    if (argc < 2) {
        shell_print("Usage: rm [-r] <file/dir>\n");
        return -1;
    }
    
    int recursive = 0;
    char* target = NULL;
    
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            for (int j = 1; argv[i][j]; j++) {
                if (argv[i][j] == 'r') recursive = 1;
            }
        } else {
            target = argv[i];
        }
    }
    
    if (!target) {
        shell_print("rm: missing operand\n");
        return -1;
    }
    
    char path[256];
    build_path(target, path);
    
    if (ufs_isdir(path)) {
        if (!recursive) {
            shell_print("rm: cannot remove '");
            shell_print(path);
            shell_print("': Is a directory\n");
            return -1;
        }
        return ufs_rmdir_force(path);
    } else {
        return ufs_delete(path);
    }
}

// ========== CP ==========
static int cmd_cp(int argc, char** argv) {
    if (argc < 3) {
        shell_print("Usage: cp <source> <dest>\n");
        return -1;
    }
    
    char src[256], dst[256];
    build_path(argv[1], src);
    build_path(argv[2], dst);
    
    if (ufs_cp(src, dst) == 0) {
        return 0;
    } else {
        shell_print("cp: failed to copy\n");
        return -1;
    }
}

// ========== MV ==========
static int cmd_mv(int argc, char** argv) {
    if (argc < 3) {
        shell_print("Usage: mv <source> <dest>\n");
        return -1;
    }
    
    char src[256], dst[256];
    build_path(argv[1], src);
    build_path(argv[2], dst);
    
    if (ufs_mv(src, dst) == 0) {
        return 0;
    } else {
        shell_print("mv: failed to move\n");
        return -1;
    }
}

// ========== DF ==========
static int cmd_df(int argc, char** argv) {
    (void)argc; (void)argv;
    
    u32 total, used, free;
    if (ufs_stat(&total, &used, &free) != 0) {
        shell_print("df: failed to get filesystem stats\n");
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

// ========== MKFS.UFS ==========
static int cmd_mkfs_ufs(int argc, char** argv) {
    u32 start_lba = 2048;
    u32 total_blocks = 0;
    int disk_idx = 0;
    
    if (argc < 2) {
        shell_print("Usage: mkfs.ufs <device> [blocks]\n");
        shell_print("Devices: /dev/sda, /dev/sdb, /dev/sdc, /dev/sdd\n");
        return -1;
    }
    
    if (argv[1][0] == '/' && argv[1][1] == 'd' && argv[1][2] == 'e' && 
        argv[1][3] == 'v' && argv[1][4] == '/' && 
        argv[1][5] == 's' && argv[1][6] == 'd') {
        
        int idx = argv[1][7] - 'a';
        if (idx >= 0 && idx <= 3) {
            disk_idx = idx;
            disk_set_disk(disk_idx);
            
            u64 sectors = disk_get_sectors(0x80 + disk_idx);
            if (sectors > 0) {
                total_blocks = sectors - 2048;
            }
        }
    }
    
    if (argc >= 3) {
        // Парсим число
        total_blocks = 0;
        for (int i = 0; argv[2][i] && argv[2][i] >= '0' && argv[2][i] <= '9'; i++) {
            total_blocks = total_blocks * 10 + (argv[2][i] - '0');
        }
    }
    
    if (total_blocks < 100) {
        shell_print("mkfs.ufs: invalid device or size\n");
        return -1;
    }
    
    shell_print("Creating UFS on ");
    shell_print(argv[1]);
    shell_print(" (");
    shell_print_num(total_blocks * 512 / (1024*1024));
    shell_print(" MB)... ");
    
    if (ufs_format(start_lba, total_blocks) == 0) {
        shell_print("OK\n");
        return 0;
    } else {
        shell_print("FAILED\n");
        return -1;
    }
}

// ========== MOUNT ==========
static int cmd_mount(int argc, char** argv) {
    u32 start_lba = 2048;
    
    if (argc > 1) {
        if (argv[1][0] == '/' && argv[1][1] == 'd' && argv[1][2] == 'e' && 
            argv[1][3] == 'v' && argv[1][4] == '/' && 
            argv[1][5] == 's' && argv[1][6] == 'd') {
            int idx = argv[1][7] - 'a';
            disk_set_disk(idx);
        }
    }
    
    if (ufs_mount(start_lba) == 0) {
        strcpy(current_dir, "/");
        shell_print("Mounted\n");
        return 0;
    } else {
        shell_print("Mount failed\n");
        return -1;
    }
}

// ========== UMOUNT ==========
static int cmd_umount(int argc, char** argv) {
    (void)argc; (void)argv;
    shell_print("umount: not implemented\n");
    return -1;
}

// ========== INIT ==========
void fs_commands_init(void) {
    shell_register_command("ls", cmd_ls, "List directory");
    shell_register_command("cd", cmd_cd, "Change directory");
    shell_register_command("pwd", cmd_pwd, "Print working directory");
    shell_register_command("mkdir", cmd_mkdir, "Create directory");
    shell_register_command("rmdir", cmd_rmdir, "Remove directory (use -f for force)");
    shell_register_command("mk", cmd_mk, "Create empty file");
    shell_register_command("echo", cmd_echo, "Echo text or write to file");
    shell_register_command("cat", cmd_cat, "Show file content");
    shell_register_command("rm", cmd_rm, "Delete file/dir (use -r for dirs)");
    shell_register_command("cp", cmd_cp, "Copy file");
    shell_register_command("mv", cmd_mv, "Move file");
    shell_register_command("df", cmd_df, "Show filesystem usage");
    shell_register_command("mkfs.ufs", cmd_mkfs_ufs, "Create UFS filesystem");
    shell_register_command("mount", cmd_mount, "Mount UFS");
    shell_register_command("umount", cmd_umount, "Unmount UFS");
}
