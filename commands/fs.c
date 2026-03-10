#include "../include/shell_api.h"
#include "../include/string.h"
#include "../fs/ufs.h"
#include "../drivers/vga.h"
#include "../kernel/memory.h"

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
    
    if (count == 0) {
        shell_print("Directory is empty\n");
        return 0;
    }
    
    shell_print(path);
    shell_print("\n");
    
    for (u32 i = 0; i < count; i++) {
        if (entries[i].is_dir) {
            shell_print("  [DIR]  ");
        } else {
            shell_print("  [FILE] ");
        }
        
        shell_print(entries[i].name);
        
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

void fs_commands_init(void) {
    shell_register_command("ls", cmd_ls, "List directory");
    shell_register_command("cd", cmd_cd, "Change directory");
    shell_register_command("pwd", cmd_pwd, "Print working directory");
    shell_register_command("mkdir", cmd_mkdir, "Create directory");
    shell_register_command("touch", cmd_touch, "Create empty file");
    shell_register_command("cat", cmd_cat, "Show file content");
    shell_register_command("rm", cmd_rm, "Delete file");
}
