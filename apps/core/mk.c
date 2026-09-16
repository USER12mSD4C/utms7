#include "../../lib/libc.h"

static void out(const char *s) {
    write(1, s, strlen(s));
}

int main(int argc, char **argv) {
    if (argc < 2) {
        out("usage: touch <file>\n");
        return 1;
    }

    int fd = open(argv[1], O_WRONLY | O_CREAT, 0644);
    if (fd < 0) {
        set_color(0x0C, 0);
        out("touch: failed\n");
        set_color(0x07, 0);
        return 1;
    }

    close(fd);
    return 0;
}
