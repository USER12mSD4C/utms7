#include "../lib/libc.h"

static void out(const char *s) {
    write(1, s, strlen(s));
}

static void put_u64(unsigned long long v) {
    char buf[24];
    int i = 0;

    if (v == 0) {
        out("0");
        return;
    }

    while (v > 0) {
        buf[i++] = (char)('0' + (v % 10));
        v /= 10;
    }

    while (i-- > 0) {
        char c = buf[i];
        write(1, &c, 1);
    }
}

int main(int argc, char **argv) {
    char cwd[256];
    getcwd(cwd, sizeof(cwd));

    const char* path = (argc > 1) ? argv[1] : cwd;
    struct dirent ents[128];

    long n = syscall(SYS_readdir, (long)path, (long)ents, 128, 0, 0, 0);
    if (n < 0) {
        out("ls: cannot access '");
        out(path);
        out("'\n");
        return 1;
    }

    for (long i = 0; i < n; i++) {
        if (ents[i].is_dir) {
            set_color(0x0B, 0);
            out(ents[i].name);
            out("/\n");
        } else {
            set_color(0x07, 0);
            out(ents[i].name);
            out("  ");
            put_u64(ents[i].size);
            out(" B\n");
        }
    }

    set_color(0x07, 0);
    return 0;
}
