#include "../lib/libc.h"

static void out(const char *s) {
    write(1, s, strlen(s));
}

static void put_num(long v) {
    char buf[24];
    int i = 0;

    if (v == 0) {
        out("0");
        return;
    }

    if (v < 0) {
        out("-");
        v = -v;
    }

    while (v > 0) {
        buf[i++] = '0' + (v % 10);
        v /= 10;
    }

    while (i-- > 0) {
        write(1, &buf[i], 1);
    }
}

int main(int argc, char **argv) {
    out("=== libc test ===\n\n");

    time_t now = time(NULL);
    out("Current time: ");
    put_num((long)now);
    out(" seconds since epoch\n");

    struct tm *tm = localtime(&now);
    char timebuf[64];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", tm);
    out("Formatted: ");
    out(timebuf);
    out("\n\n");

    out("getopt test:\n");
    int c;
    optind = 1;
    char *test_argv[] = {"test", "-a", "-b", "value", "-c", NULL};
    int test_argc = 5;

    while ((c = getopt(test_argc, test_argv, "ab:c")) != -1) {
        out("  option: ");
        char cb[2] = {(char)c, '\0'};
        out(cb);
        if (optarg) {
            out(" arg=");
            out(optarg);
        }
        out("\n");
    }
    out("\n");

    out("opendir/readdir test:\n");
    DIR *dir = opendir("/");
    if (dir) {
        struct dirent *ent;
        int count = 0;
        while ((ent = readdir(dir)) != NULL && count < 5) {
            out("  ");
            out(ent->name);
            if (ent->is_dir) out("/");
            out("\n");
            count++;
        }
        closedir(dir);
    } else {
        out("  failed to open /\n");
    }
    out("\n");

    out("fcntl test:\n");
    int fd = open("test_libc.tmp", O_WRONLY | O_CREAT, 0644);
    if (fd >= 0) {
        int flags = fcntl(fd, F_GETFL);
        out("  fd=");
        put_num(fd);
        out(" flags=");
        put_num(flags);
        out("\n");
        close(fd);
        unlink("test_libc.tmp");
    } else {
        out("  failed to create file\n");
    }

    out("\n=== all tests done ===\n");
    return 0;
}
