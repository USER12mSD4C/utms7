#include "../lib/libc.h"

static void out(const char *s) {
    write(1, s, strlen(s));
}

int main(int argc, char **argv) {
    if (argc < 2) {
        out("usage: rm <path>\n");
        return 1;
    }

    if (unlink(argv[1]) != 0 && rmdir(argv[1]) != 0) {
        set_color(0x0C, 0);
        out("rm: cannot remove ");
        out(argv[1]);
        out("\n");
        set_color(0x07, 0);
        return 1;
    }

    return 0;
}
