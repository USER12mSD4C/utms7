#include "../../lib/libc.h"

static void out(const char *s) {
    write(1, s, strlen(s));
}

int main(int argc, char **argv) {
    if (argc < 2) {
        out("usage: rmdir <dir>\n");
        return 1;
    }

    if (rmdir(argv[1]) != 0) {
        set_color(0x0C, 0);
        out("rmdir: failed\n");
        set_color(0x07, 0);
        return 1;
    }

    return 0;
}
