#include "../lib/libc.h"

static void out(const char *s) {
    write(1, s, strlen(s));
}

int main(int argc, char **argv) {
    if (argc < 2) {
        out("usage: mount <dev> [point]\n");
        return 1;
    }

    const char *point = (argc >= 3) ? argv[2] : "/";

    if (partition_mount(argv[1], point) == 0) {
        return 0;
    }

    out("mount failed\n");
    return 1;
}
