#include "../../lib/libc.h"

static void out(const char *s) {
    write(1, s, strlen(s));
}

int main(int argc, char **argv) {
    if (argc < 2) {
        out("usage: mkdir <dir>\n");
        return 1;
    }

    if (mkdir(argv[1], 0755) != 0) {
        set_color(0x0C, 0);
        out("mkdir: failed\n");
        set_color(0x07, 0);
        return 1;
    }

    return 0;
}
