#include "../lib/libc.h"

static void out(const char *s) {
    write(1, s, strlen(s));
}

static int cat_fd(int fd) {
    char buf[512];
    ssize_t r;

    while ((r = read(fd, buf, sizeof(buf))) > 0) {
        if (write(1, buf, (size_t)r) != r) {
            return -1;
        }
    }

    return (r < 0) ? -1 : 0;
}

int main(int argc, char **argv) {
    if (argc == 1) {
        return cat_fd(0);
    }

    int ret = 0;

    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            set_color(0x0C, 0);
            out("cat: cannot open ");
            out(argv[i]);
            out("\n");
            set_color(0x07, 0);
            ret = -1;
            continue;
        }

        if (cat_fd(fd) != 0) ret = -1;
        close(fd);
    }

    return ret;
}
