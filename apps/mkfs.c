#include "../lib/libc.h"

static void out(const char *s) {
    write(1, s, strlen(s));
}

int main(int argc, char **argv) {
    if (argc < 2) {
        out("usage: mkfs <dev> [fstype]\n");
        return 1;
    }

    const char *fstype = (argc >= 3) ? argv[2] : "ufs";

    out("mkfs: dev=");
    out(argv[1]);
    out(" fstype=");
    out(fstype);
    out("\n");

    int result = partition_format(argv[1], fstype);

    out("mkfs: partition_format returned ");
    char buf[16];
    int i = 0;
    int v = result;
    if (v < 0) { out("-"); v = -v; }
    if (v == 0) { out("0"); } else {
        while (v > 0) { buf[i++] = (char)('0' + (v % 10)); v /= 10; }
        while (i-- > 0) { char c = buf[i]; write(1, &c, 1); }
    }
    out("\n");

    if (result == 0) {
        return 0;
    }

    out("mkfs failed\n");
    return 1;
}
