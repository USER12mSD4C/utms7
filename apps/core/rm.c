#include "../../lib/libc.h"

static void out(const char *s) {
    write(1, s, strlen(s));
}

static void err(const char *s) {
    set_color(0x0C, 0);
    out(s);
    set_color(0x07, 0);
}

static int remove_recursive(const char *path) {
    DIR *dir = opendir(path);
    if (!dir) {
        if (unlink(path) == 0) return 0;
        err("rm: cannot remove '");
        out(path);
        out("': No such file or directory\n");
        return -1;
    }

    struct linux_dirent64 *ent;
    char child[512];

    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;

        strcpy(child, path);
        if (path[strlen(path) - 1] != '/') strcat(child, "/");
        strcat(child, ent->d_name);

        if (ent->d_type == 4) {
            if (remove_recursive(child) != 0) {
                closedir(dir);
                return -1;
            }
        } else {
            if (unlink(child) != 0) {
                err("rm: cannot remove '");
                out(child);
                out("'\n");
                closedir(dir);
                return -1;
            }
        }
    }

    closedir(dir);

    if (rmdir(path) != 0) {
        err("rm: cannot remove directory '");
        out(path);
        out("'\n");
        return -1;
    }

    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        out("usage: rm [-r] <path>...\n");
        return 1;
    }

    int recursive = 0;
    int start_idx = 1;

    if (strcmp(argv[1], "-r") == 0 || strcmp(argv[1], "-R") == 0) {
        recursive = 1;
        start_idx = 2;
    }

    if (start_idx >= argc) {
        out("usage: rm [-r] <path>...\n");
        return 1;
    }

    int ret = 0;

    for (int i = start_idx; i < argc; i++) {
        const char *path = argv[i];
        DIR *dir = opendir(path);

        if (dir) {
            closedir(dir);
            if (!recursive) {
                err("rm: cannot remove '");
                out(path);
                out("': Is a directory\n");
                ret = 1;
                continue;
            }
            if (remove_recursive(path) != 0) ret = 1;
        } else {
            if (unlink(path) != 0) {
                err("rm: cannot remove '");
                out(path);
                out("': No such file or directory\n");
                ret = 1;
            }
        }
    }

    return ret;
}
