#include "../../lib/libc.h"

static void out(const char *s) {
    write(1, s, strlen(s));
}

static void put_u64(unsigned long long v) {
    char buf[24];
    int i = 0;

    if (v == 0) {
        out("0");
        return;
    }

    while (v > 0) {
        buf[i++] = (char)('0' + (v % 10));
        v /= 10;
    }

    while (i-- > 0) {
        char c = buf[i];
        write(1, &c, 1);
    }
}

int main(int argc, char **argv) {
    char cwd[256];
    getcwd(cwd, sizeof(cwd));

    const char* path = (argc > 1) ? argv[1] : cwd;
    DIR *dir = opendir(path);
    if (!dir) {
        out("ls: cannot access '");
        out(path);
        out("'\n");
        return 1;
    }

    struct linux_dirent64 *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;

        if (ent->d_type == 4) {
            set_color(0x0B, 0);
            out(ent->d_name);
            out("/\n");
        } else {
            set_color(0x07, 0);
            out(ent->d_name);
            char full_path[512];
            if (strcmp(path, "/") == 0) {
                snprintf(full_path, sizeof(full_path), "/%s", ent->d_name);
            } else {
                snprintf(full_path, sizeof(full_path), "%s/%s", path, ent->d_name);
            }
            struct stat st;
            if (stat(full_path, &st) == 0) {
                out("  ");
                put_u64(st.st_size);
                out(" B");
            }
            out("\n");
        }
    }
    closedir(dir);

    set_color(0x07, 0);
    return 0;
}
