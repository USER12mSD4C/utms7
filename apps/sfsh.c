#include "../lib/libc.h"
#include "../include/syscall.h"

#define MAX_LINE 512

#define COL_RESET 0x07
#define COL_USER  0x0B
#define COL_PATH  0x0A
#define COL_BRACE 0x0D
#define COL_ERR   0x0C
#define COL_DIR   0x0B
#define COL_OK    0x0A

typedef struct {
    int pid;
    int ppid;
    char name[32];
    int state;
} ps_entry_t;

static char cwd[256] = "/";

static void out(const char *s) {
    write(1, s, strlen(s));
}

static void put_u64(unsigned long long v) {
    char buf[24];
    int i = 0;
    if (v == 0) { out("0"); return; }
    while (v > 0) { buf[i++] = (char)('0' + (v % 10)); v /= 10; }
    while (i-- > 0) { char c = buf[i]; write(1, &c, 1); }
}

static void refresh_cwd(void) {
    if (!getcwd(cwd, sizeof(cwd))) strcpy(cwd, "/");
}

static void print_prompt(void) {
    char p[300];
    if (cwd[0] == '/' && cwd[1] == '\0') strcpy(p, "~");
    else if (cwd[0] == '/') { p[0] = '~'; strcpy(p + 1, cwd); }
    else strcpy(p, cwd);

    set_color(COL_BRACE, 0); out("{");
    set_color(COL_USER, 0);  out("user12ms@utms");
    set_color(COL_RESET, 0); out("; ");
    set_color(COL_PATH, 0);  out(p);
    set_color(COL_BRACE, 0); out("}");
    set_color(COL_OK, 0);    out("$ ");
    set_color(COL_RESET, 0);
}

static int cmd_help(int argc, char **argv) {
    (void)argc; (void)argv;
    out("UTMS7 shell commands:\n");
    out("  help          this list\n");
    out("  clear         clear screen\n");
    out("  echo <args>   print args\n");
    out("  pwd           current directory\n");
    out("  cd <dir>      change directory\n");
    out("  ls [dir]      list directory\n");
    out("  cat <file>    print file\n");
    out("  mkdir <dir>   create directory\n");
    out("  touch <file>  create empty file\n");
    out("  rm <path>     remove file\n");
    out("  write <f> <t> write text to file\n");
    out("  ps            processes\n");
    out("  mem           memory usage\n");
    out("  uptime        seconds since boot\n");
    out("  uname         system info\n");
    return 0;
}

static int cmd_clear(int argc, char **argv) {
    (void)argc; (void)argv;
    clear_screen();
    return 0;
}

static int cmd_echo(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        out(argv[i]);
        if (i < argc - 1) out(" ");
    }
    out("\n");
    return 0;
}

static int cmd_pwd(int argc, char **argv) {
    (void)argc; (void)argv;
    refresh_cwd();
    out(cwd);
    out("\n");
    return 0;
}

static int cmd_cd(int argc, char **argv) {
    if (argc < 2) {
        chdir("/");
        refresh_cwd();
        return 0;
    }
    if (chdir(argv[1]) != 0) {
        set_color(COL_ERR, 0);
        out("cd: no such directory: ");
        out(argv[1]);
        out("\n");
        set_color(COL_RESET, 0);
        return -1;
    }
    refresh_cwd();
    return 0;
}

static int cmd_ls(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : cwd;
    struct dirent ents[128];
    long n = syscall(SYS_readdir, (long)path, (long)ents, 128, 0, 0, 0);
    if (n < 0) {
        set_color(COL_ERR, 0);
        out("ls: cannot access '");
        out(path);
        out("'\n");
        set_color(COL_RESET, 0);
        return -1;
    }
    for (long i = 0; i < n; i++) {
        if (ents[i].is_dir) {
            set_color(COL_DIR, 0);
            out(ents[i].name);
            out("/\n");
        } else {
            set_color(COL_RESET, 0);
            out(ents[i].name);
            out("  ");
            put_u64(ents[i].size);
            out(" B\n");
        }
    }
    set_color(COL_RESET, 0);
    return 0;
}

static int cmd_cat(int argc, char **argv) {
    if (argc < 2) {
        out("usage: cat <file>\n");
        return -1;
    }
    int fd = open(argv[1], 0);
    if (fd < 0) {
        set_color(COL_ERR, 0);
        out("cat: cannot open '");
        out(argv[1]);
        out("'\n");
        set_color(COL_RESET, 0);
        return -1;
    }
    char buf[512];
    ssize_t r;
    while ((r = read(fd, buf, sizeof(buf))) > 0) {
        write(1, buf, (size_t)r);
    }
    close(fd);
    return 0;
}

static int cmd_mkdir(int argc, char **argv) {
    if (argc < 2) { out("usage: mkdir <dir>\n"); return -1; }
    if (mkdir(argv[1], 0755) != 0) {
        set_color(COL_ERR, 0);
        out("mkdir: failed\n");
        set_color(COL_RESET, 0);
        return -1;
    }
    return 0;
}

static int cmd_touch(int argc, char **argv) {
    if (argc < 2) { out("usage: touch <file>\n"); return -1; }
    int fd = open(argv[1], 0x41, 0644);
    if (fd < 0) {
        set_color(COL_ERR, 0);
        out("touch: failed\n");
        set_color(COL_RESET, 0);
        return -1;
    }
    close(fd);
    return 0;
}

static int cmd_rm(int argc, char **argv) {
    if (argc < 2) { out("usage: rm <path>\n"); return -1; }
    if (unlink(argv[1]) != 0 && rmdir(argv[1]) != 0) {
        set_color(COL_ERR, 0);
        out("rm: cannot remove '");
        out(argv[1]);
        out("'\n");
        set_color(COL_RESET, 0);
        return -1;
    }
    return 0;
}

static int cmd_write(int argc, char **argv) {
    if (argc < 3) { out("usage: write <file> <text...>\n"); return -1; }
    int fd = open(argv[1], 0x41, 0644);
    if (fd < 0) {
        set_color(COL_ERR, 0);
        out("write: cannot open '");
        out(argv[1]);
        out("'\n");
        set_color(COL_RESET, 0);
        return -1;
    }
    for (int i = 2; i < argc; i++) {
        write(fd, argv[i], strlen(argv[i]));
        if (i < argc - 1) write(fd, " ", 1);
    }
    write(fd, "\n", 1);
    close(fd);
    return 0;
}

static int cmd_ps(int argc, char **argv) {
    (void)argc; (void)argv;
    ps_entry_t ents[32];
    long n = syscall(SYS_ps, (long)ents, 32, 0, 0, 0, 0);
    if (n <= 0) { out("no processes\n"); return 0; }
    out("PID  PPID  ST  NAME\n");
    for (long i = 0; i < n; i++) {
        put_u64((unsigned long long)ents[i].pid);
        out("    ");
        put_u64((unsigned long long)ents[i].ppid);
        out("    ");
        switch (ents[i].state) {
            case 1: out("R   "); break;
            case 2: out("R   "); break;
            case 3: out("S   "); break;
            case 4: out("B   "); break;
            case 5: out("Z   "); break;
            default: out("?   "); break;
        }
        out(ents[i].name);
        out("\n");
    }
    return 0;
}

static int cmd_mem(int argc, char **argv) {
    (void)argc; (void)argv;
    unsigned long long t = 0, u = 0, f = 0;
    syscall(SYS_meminfo, (long)&t, (long)&u, (long)&f, 0, 0, 0);
    out("total: "); put_u64(t / 1024); out(" KB\n");
    out("used:  "); put_u64(u / 1024); out(" KB\n");
    out("free:  "); put_u64(f / 1024); out(" KB\n");
    return 0;
}

static int cmd_uptime(int argc, char **argv) {
    (void)argc; (void)argv;
    unsigned long s = time();
    unsigned long h = s / 3600;
    unsigned long m = (s % 3600) / 60;
    unsigned long sec = s % 60;
    out("up ");
    if (h) { put_u64(h); out("h "); }
    if (h || m) { put_u64(m); out("m "); }
    put_u64(sec);
    out("s\n");
    return 0;
}

static int cmd_uname(int argc, char **argv) {
    (void)argc; (void)argv;
    out("UTMS7 0.2 x86_64 hybrid kernel\n");
    return 0;
}

typedef struct {
    const char *name;
    int (*func)(int, char**);
} sh_cmd_t;

static int cmd_disks(int argc, char **argv) {
    (void)argc; (void)argv;
    // File: apps/sfsh.c
    // Найди typedef struct { ... } disk_info_user_t; и замени на:

        typedef struct {
            u8 present;
            u8 disk_num;
            char model[41];
            u64 total_sectors;
            u32 sector_size;
            u8 partition_count;
            struct {
                u8 present;
                u8 disk_num;
                u8 partition_num;
                u64 start_lba;
                u64 end_lba;
                u64 size;
                int type;
                char name[32];
            } __attribute__((packed)) partitions[16];
            u8 is_gpt;
        } __attribute__((packed)) disk_info_user_t;

    disk_info_user_t disks[4];
    int n = disk_list(disks, 4);
    if (n <= 0) { out("no disks\n"); return 0; }

    for (int i = 0; i < n; i++) {
        if (!disks[i].present) continue;
        set_color(COL_DIR, 0);
        out("/dev/sd");
        char c = 'a' + i;
        write(1, &c, 1);
        set_color(COL_RESET, 0);
        out("  ");
        put_u64(disks[i].total_sectors * 512 / (1024*1024));
        out(" MB  ");
        out(disks[i].model);
        out(disks[i].is_gpt ? "  GPT\n" : "  MBR\n");

        for (int j = 0; j < disks[i].partition_count; j++) {
            if (!disks[i].partitions[j].present) continue;
            out("  /dev/sd");
            char c2 = 'a' + i;
            write(1, &c2, 1);
            put_u64(disks[i].partitions[j].partition_num);
            out("  ");
            put_u64(disks[i].partitions[j].size / (1024*1024));
            out(" MB  ");
            switch(disks[i].partitions[j].type) {
                case 1: set_color(COL_OK, 0); out("UFS\n"); break;
                case 2: out("FAT32\n"); break;
                case 3: out("EXT4\n"); break;
                default: out("unknown\n"); break;
            }
            set_color(COL_RESET, 0);
        }
    }
    return 0;
}

static int cmd_mount(int argc, char **argv) {
    if (argc < 2) { out("usage: mount /dev/sdX[1-16] [point]\n"); return -1; }
    const char* point = (argc >= 3) ? argv[2] : "/";
    if (partition_mount(argv[1], point) != 0) {
        set_color(COL_ERR, 0);
        out("mount failed\n");
        set_color(COL_RESET, 0);
        return -1;
    }
    refresh_cwd();
    return 0;
}

static int cmd_umount(int argc, char **argv) {
    (void)argc; (void)argv;
    if (partition_umount() != 0) {
        set_color(COL_ERR, 0);
        out("umount failed\n");
        set_color(COL_RESET, 0);
        return -1;
    }
    strcpy(cwd, "/");
    return 0;
}

static int cmd_mkfs(int argc, char **argv) {
    if (argc < 2) { out("usage: mkfs.ufs /dev/sdX[1-16]\n"); return -1; }
    if (partition_format(argv[1], "ufs") != 0) {
        set_color(COL_ERR, 0);
        out("format failed\n");
        set_color(COL_RESET, 0);
        return -1;
    }
    set_color(COL_OK, 0);
    out("formatted\n");
    set_color(COL_RESET, 0);
    return 0;
}

static int cmd_mktable(int argc, char **argv) {
    if (argc < 3) { out("usage: mktable /dev/sdX <mbr|gpt>\n"); return -1; }
    int gpt = (strcmp(argv[2], "gpt") == 0);
    if (!gpt && strcmp(argv[2], "mbr") != 0) { out("unknown table type\n"); return -1; }
    if (disk_create_table(argv[1], gpt) != 0) {
        set_color(COL_ERR, 0); out("failed\n"); set_color(COL_RESET, 0);
        return -1;
    }
    set_color(COL_OK, 0); out("table created\n"); set_color(COL_RESET, 0);
    return 0;
}

static int cmd_mkpart(int argc, char **argv) {
    if (argc < 3) { out("usage: mkpart /dev/sdX <sizeMB>\n"); return -1; }
    unsigned long mb = 0;
    for (char *p = argv[2]; *p; p++) {
        if (*p < '0' || *p > '9') { out("invalid size\n"); return -1; }
        mb = mb * 10 + (unsigned long)(*p - '0');
    }
    if (partition_create(argv[1], mb, 1) != 0) {
        set_color(COL_ERR, 0); out("failed\n"); set_color(COL_RESET, 0);
        return -1;
    }
    set_color(COL_OK, 0); out("partition created\n"); set_color(COL_RESET, 0);
    return 0;
}

static int cmd_rmpart(int argc, char **argv) {
    if (argc < 2) { out("usage: rmpart /dev/sdX1\n"); return -1; }
    if (partition_delete(argv[1]) != 0) {
        set_color(COL_ERR, 0); out("failed\n"); set_color(COL_RESET, 0);
        return -1;
    }
    set_color(COL_OK, 0); out("partition deleted\n"); set_color(COL_RESET, 0);
    return 0;
}

static const sh_cmd_t cmds[] = {
    { "help", cmd_help },
    { "clear", cmd_clear },
    { "echo", cmd_echo },
    { "pwd", cmd_pwd },
    { "cd", cmd_cd },
    { "ls", cmd_ls },
    { "cat", cmd_cat },
    { "mkdir", cmd_mkdir },
    { "touch", cmd_touch },
    { "rm", cmd_rm },
    { "write", cmd_write },
    { "ps", cmd_ps },
    { "mem", cmd_mem },
    { "uptime", cmd_uptime },
    { "uname", cmd_uname },
    { "disks", cmd_disks },
    { "lsblk", cmd_disks },
    { "mount", cmd_mount },
    { "umount", cmd_umount },
    { "mkfs.ufs", cmd_mkfs },
    { "mktable", cmd_mktable },
    { "mkpart", cmd_mkpart },
    { "rmpart", cmd_rmpart },
};

int main(void) {
    write(1, "DEBUG: main started\n", 20);
    set_color(COL_RESET, 0);
    write(1, "DEBUG: after set_color\n", 23);
    refresh_cwd();
    write(1, "DEBUG: after refresh_cwd\n", 25);
    out("UTMS7 shell ready. Type 'help'.\n");
    write(1, "DEBUG: after first out\n", 23);

    char line[MAX_LINE];
    int pos;
    char c;

    while (1) {
        print_prompt();
        pos = 0;
        line[0] = '\0';

        while (1) {
            if (read(0, &c, 1) <= 0) continue;

            if (c == '\b' || c == 0x7F || c == 0x0E) {
                if (pos > 0) {
                    pos--;
                    write(1, "\b \b", 3);
                }
                continue;
            }

            if (c == '\n' || c == '\r') {
                line[pos] = '\0';
                write(1, "\n", 1);
                break;
            }

            if (c >= 32 && c <= 126) {
                if (pos < MAX_LINE - 1) {
                    line[pos++] = c;
                    write(1, &c, 1);
                }
            }
        }

        if (pos == 0) continue;

        char *argv[16];
        int argc = 0;
        char *p = line;

        while (*p && argc < 15) {
            while (*p == ' ' || *p == '\t') p++;
            if (!*p) break;

            argv[argc++] = p;

            while (*p && *p != ' ' && *p != '\t') p++;
            if (*p) *p++ = '\0';
        }

        argv[argc] = NULL;

        if (argc == 0) continue;

        int found = 0;
        for (unsigned i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
            if (strcmp(cmds[i].name, argv[0]) == 0) {
                cmds[i].func(argc, argv);
                found = 1;
                break;
            }
        }
        if (!found) {
            int pid = fork();
            if (pid == 0) {
                execve(argv[0], argv, NULL);
                set_color(COL_ERR, 0);
                out("sh: command not found: ");
                out(argv[0]);
                out("\n");
                set_color(COL_RESET, 0);
                _exit(1);
            } else if (pid > 0) {
                int status;
                waitpid(pid, &status, 0);
            } else {
                set_color(COL_ERR, 0);
                out("sh: fork failed\n");
                set_color(COL_RESET, 0);
            }
        }
        set_color(COL_RESET, 0);
    }
    return 0;
}
