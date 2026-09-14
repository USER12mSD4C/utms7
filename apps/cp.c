#include "../lib/libc.h"

static void out(const char *s) {
    write(1, s, strlen(s));
}

int main(int argc, char **argv) {
    if (argc < 3) {
        out("usage: cp <source> <dest>\n");
        return 1;
    }

    int src = open(argv[1], O_RDONLY);
    if (src < 0) {
        out("cp: cannot open source\n");
        return 1;
    }

    int dst = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst < 0) {
        out("cp: cannot open dest\n");
        close(src);
        return 1;
    }

    char buf[512];
    ssize_t r;

    while ((r = read(src, buf, sizeof(buf))) > 0) {
        if (write(dst, buf, (size_t)r) != r) {
            out("cp: write failed\n");
            close(src);
            close(dst);
            return 1;
        }
    }

    close(src);
    close(dst);
    return 0;
}
