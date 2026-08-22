#include "../lib/libc.h"

static void out(const char *s) {
    write(1, s, strlen(s));
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    if (partition_umount() == 0) {
        return 0;
    }

    out("umount failed\n");
    return 1;
}
