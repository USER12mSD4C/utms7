#include "../include/shell_api.h"
#include "../include/string.h"
#include "../fs/ufs.h"
#include "../drivers/vga.h"
#include "../kernel/memory.h"
#include "../drivers/disk.h"
#include "../include/path.h"

static char current_dir[256] = "/";

const char* fs_get_current_dir(void) {
    return current_dir;
}

void fs_set_current_dir(const char* path) {
    if (path && path[0]) {
        strcpy(current_dir, path);
    }
}

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
        strcpy(path, fs_get_current_dir());
    }

    FSNode *entries = NULL;
    u32 count = 0;

    int res = ufs_readdir(path, &entries, &count);

    if (res != 0) {
        // Если директория не существует
        if (!ufs_exists(path)) {
            shell_print("ls: cannot access '");
            shell_print(path);
            shell_print("': No such file or directory\n");
        } else if (!ufs_isdir(path)) {
            shell_print("ls: ");
            shell_print(path);
            shell_print(": Not a directory\n");
        } else {
            shell_print("ls: cannot access '");
            shell_print(path);
            shell_print("'\n");
        }
        return -1;
    }

    if (count == 0) {
        if (entries) kfree(entries);
        return 0;
    }

    for (u32 i = 0; i < count; i++) {
        if (!show_all && entries[i].name[0] == '.') continue;

        if (entries[i].is_dir) shell_print("d ");
        else shell_print("- ");

        shell_print(entries[i].name);

        int len = strlen(entries[i].name);
        for (int j = len; j < 20; j++) shell_print(" ");

        shell_print_num(entries[i].size);
        shell_print(" B\n");
    }

    if (entries) kfree(entries);
    return 0;
}

static int cmd_cd(int argc, char** argv) {
    char path[256];

    if (argc < 2) {
        fs_set_current_dir("/");
        return 0;
    }

    build_path(argv[1], path);

    if (!ufs_isdir(path)) {
        shell_print("cd: not a directory\n");
        return -1;
    }

    fs_set_current_dir(path);
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

    int success = 0;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') continue;

        char path[256];
        build_path(argv[i], path);

        if (ufs_mkdir(path) == 0) success++;
    }

    return (success > 0) ? 0 : -1;
}

static int cmd_mk(int argc, char** argv) {
    if (argc < 2) {
        shell_print("Usage: mk <file>\n");
        return -1;
    }

    int success = 0;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') continue;

        char path[256];
        build_path(argv[i], path);

        if (ufs_write(path, NULL, 0) == 0) success++;
    }

    return (success > 0) ? 0 : -1;
}

static int cmd_rm(int argc, char** argv) {
    if (argc < 2) {
        shell_print("Usage: rm [-r] <file>\n");
        return -1;
    }

    int recursive = 0;
    int success = 0;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            for (int j = 1; argv[i][j]; j++) {
                if (argv[i][j] == 'r') recursive = 1;
            }
            continue;
        }

        char path[256];
        build_path(argv[i], path);

        if (ufs_isdir(path)) {
            if (!recursive) {
                shell_print("rm: is a directory\n");
                continue;
            }
            if (ufs_rmdir_force(path) == 0) success++;
        } else {
            if (ufs_delete(path) == 0) success++;
        }
    }

    return (success > 0) ? 0 : -1;
}

static int cmd_cat(int argc, char** argv) {
    if (argc < 2) {
        shell_print("Usage: cat <file>\n");
        return -1;
    }

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') continue;

        char path[256];
        build_path(argv[i], path);

        u8 *data = NULL;
        u32 size = 0;

        if (ufs_read(path, &data, &size) != 0) {
            shell_print("cat: file not found\n");
            continue;
        }

        if (size > 0) {
            shell_print((char*)data);
            if (data[size-1] != '\n') shell_print("\n");
        }

        if (data) kfree(data);
    }

    return 0;
}

static int cmd_df(int argc, char** argv) {
    (void)argc; (void)argv;

    if (!ufs_ismounted()) {
        shell_print("df: no filesystem mounted\n");
        return -1;
    }

    u32 total, used, free;
    if (ufs_stat(&total, &used, &free) != 0) return -1;

    shell_print("UFS on ");
    shell_print(ufs_get_device());
    shell_print(" mounted on ");
    shell_print(ufs_get_mount_point());
    shell_print("\n");
    shell_print("  total: "); shell_print_num(total / 1024); shell_print(" KB\n");
    shell_print("  used:  "); shell_print_num(used / 1024); shell_print(" KB\n");
    shell_print("  free:  "); shell_print_num(free / 1024); shell_print(" KB\n");

    return 0;
}

int fs_commands_init(void) {
    shell_register_command("ls", cmd_ls, "list directory");
    shell_register_command("cd", cmd_cd, "change directory");
    shell_register_command("pwd", cmd_pwd, "print working directory");
    shell_register_command("mkdir", cmd_mkdir, "create directory");
    shell_register_command("mk", cmd_mk, "create file");
    shell_register_command("rm", cmd_rm, "remove file/dir");
    shell_register_command("cat", cmd_cat, "show file");
    shell_register_command("df", cmd_df, "fs usage");
    return 0;
}
