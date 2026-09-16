#include "../lib/libc.h"

static void out(const char *s) {
    write(1, s, strlen(s));
}

static void err(const char *s) {
    set_color(0x0C, 0);
    out(s);
    set_color(0x07, 0);
}

static int copy_file(const char *src, const char *dst) {
    int sfd = open(src, O_RDONLY);
    if (sfd < 0) {
        err("cp: cannot open '");
        out(src);
        out("': No such file or directory\n");
        return -1;
    }

    int dfd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dfd < 0) {
        err("cp: cannot create '");
        out(dst);
        out("'\n");
        close(sfd);
        return -1;
    }

    char buf[4096];
    ssize_t r;
    int ret = 0;

    while ((r = read(sfd, buf, sizeof(buf))) > 0) {
        if (write(dfd, buf, (size_t)r) != r) {
            err("cp: write failed\n");
            ret = -1;
            break;
        }
    }

    if (r < 0) {
        err("cp: read failed\n");
        ret = -1;
    }

    close(sfd);
    close(dfd);
    return ret;
}

static int copy_recursive(const char *src, const char *dst) {
    DIR *dir = opendir(src);
    if (!dir) {
        return copy_file(src, dst);
    }

    if (mkdir(dst, 0755) != 0) {
        struct stat st;
        if (stat(dst, &st) != 0) {
            err("cp: cannot create directory '");
            out(dst);
            out("'\n");
            closedir(dir);
            return -1;
        }
    }

    struct linux_dirent64 *ent;
    char src_child[512];
    char dst_child[512];
    int ret = 0;

    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;

        strcpy(src_child, src);
        if (src[strlen(src) - 1] != '/') strcat(src_child, "/");
        strcat(src_child, ent->d_name);

        strcpy(dst_child, dst);
        if (dst[strlen(dst) - 1] != '/') strcat(dst_child, "/");
        strcat(dst_child, ent->d_name);

        if (ent->d_type == 4) {
            if (copy_recursive(src_child, dst_child) != 0) ret = -1;
        } else {
            if (copy_file(src_child, dst_child) != 0) ret = -1;
        }
    }

    closedir(dir);
    return ret;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        out("usage: cp [-r] <source> <dest>\n");
        return 1;
    }

    int recursive = 0;
    int start_idx = 1;

    if (strcmp(argv[1], "-r") == 0 || strcmp(argv[1], "-R") == 0) {
        recursive = 1;
        start_idx = 2;
    }

    if (start_idx + 2 > argc) {
        out("usage: cp [-r] <source> <dest>\n");
        return 1;
    }

    const char *src = argv[start_idx];
    const char *dst = argv[start_idx + 1];

    DIR *src_dir = opendir(src);
    if (src_dir) {
        closedir(src_dir);
        if (!recursive) {
            err("cp: -r not specified; omitting directory '");
            out(src);
            out("'\n");
            return 1;
        }
    }

    DIR *dst_dir = opendir(dst);
    char final_dst[512];

    if (dst_dir) {
        closedir(dst_dir);
        strcpy(final_dst, dst);
        if (dst[strlen(dst) - 1] != '/') strcat(final_dst, "/");

        const char *base = strrchr(src, '/');
        base = base ? base + 1 : src;
        strcat(final_dst, base);
    } else {
        strcpy(final_dst, dst);
    }

    return copy_recursive(src, final_dst) == 0 ? 0 : 1;
}
