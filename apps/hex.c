#include "../lib/libc.h"

static void out(const char *s) {
    write(1, s, strlen(s));
}

static void put_hex(unsigned long long v, int digits) {
    char buf[17];
    buf[digits] = '\0';
    for (int i = digits - 1; i >= 0; i--) {
        int d = v & 0xF;
        buf[i] = d < 10 ? '0' + d : 'a' + d - 10;
        v >>= 4;
    }
    write(1, buf, digits);
}

static void put_u64(unsigned long long v) {
    char buf[24];
    int i = 0;
    if (v == 0) { out("0"); return; }
    while (v > 0) { buf[i++] = '0' + (v % 10); v /= 10; }
    while (i-- > 0) write(1, &buf[i], 1);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        out("usage: hex <file>\n");
        return 1;
    }

    int fd = open(argv[1], 0);
    if (fd < 0) {
        out("hex: cannot open ");
        out(argv[1]);
        out("\n");
        return 1;
    }

    struct stat st;
    fstat(fd, &st);

    out("File: ");
    out(argv[1]);
    out(" (");
    put_u64(st.st_size);
    out(" bytes)\n\n");

    unsigned char buf[16];
    unsigned long offset = 0;

    while (offset < st.st_size) {
        put_hex(offset, 8);
        out("  ");

        long n = read(fd, buf, 16);
        if (n <= 0) break;

        for (long i = 0; i < 16; i++) {
            if (i < n) {
                put_hex(buf[i], 2);
                out(" ");
            } else {
                out("   ");
            }
            if (i == 7) out(" ");
        }

        out(" |");
        for (long i = 0; i < n; i++) {
            char c = (buf[i] >= 32 && buf[i] < 127) ? (char)buf[i] : '.';
            write(1, &c, 1);
        }
        out("|\n");

        offset += n;
    }

    close(fd);
    return 0;
}
