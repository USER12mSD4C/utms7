#include "../lib/libc.h"
#include "../include/syscall.h"

#define MAX_LINE 512
#define MAX_TOKENS 64
#define MAX_ARGS 32

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
static char heredoc_file[64];

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

static void refresh_cwd(void) {
    if (!getcwd(cwd, sizeof(cwd))) {
        strcpy(cwd, "/");
    }
}

static void print_prompt(void) {
    char p[300];

    if (cwd[0] == '/' && cwd[1] == '\0') {
        strcpy(p, "~");
    } else if (cwd[0] == '/') {
        p[0] = '~';
        strcpy(p + 1, cwd);
    } else {
        strcpy(p, cwd);
    }

    set_color(COL_BRACE, 0);
    out("{");
    set_color(COL_USER, 0);
    out("user12ms@utms");
    set_color(COL_RESET, 0);
    out("; ");
    set_color(COL_PATH, 0);
    out(p);
    set_color(COL_BRACE, 0);
    out("}");
    set_color(COL_OK, 0);
    out("$ ");
    set_color(COL_RESET, 0);
}

static int cmd_help(int argc, char **argv) {
    (void)argc;
    (void)argv;

    out("UTMS7 shell commands:\n");
    out("  help          this list\n");
    out("  exit          leave shell\n");
    out("  clear         clear screen\n");
    out("  echo <args>   print args\n");
    out("  pwd           current directory\n");
    out("  cd <dir>      change directory\n");
    out("  ls [dir]      list directory\n");
    out("  cat [files]   copy input/files to output\n");
    out("  mkdir <dir>   create directory\n");
    out("  touch <file>  create file\n");
    out("  rm <path>     remove file or directory\n");
    out("  ps            process list\n");
    out("  mem           memory usage\n");
    out("  uptime        seconds since boot\n");
    out("  uname         system info\n");
    out("\nRedirection:\n");
    out("  cmd > file\n");
    out("  cmd >> file\n");
    out("  cmd < file\n");
    out("  cmd <<EOF\n");

    return 0;
}

static int cmd_exit(int argc, char **argv) {
    (void)argc;
    (void)argv;
    _exit(0);
    return 0;
}

static int cmd_clear(int argc, char **argv) {
    (void)argc;
    (void)argv;
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
    (void)argc;
    (void)argv;

    refresh_cwd();
    out(cwd);
    out("\n");
    return 0;
}

static int cmd_cd(int argc, char **argv) {
    int res;

    if (argc < 2) {
        res = chdir("/");
    } else {
        res = chdir(argv[1]);
    }

    if (res != 0) {
        set_color(COL_ERR, 0);
        out("cd: cannot change directory: ");
        if (argc < 2) out("/");
        else out(argv[1]);
        out("\n");
        set_color(COL_RESET, 0);
        return -1;
    }

    refresh_cwd();
    return 0;
}

static int cmd_ls(int argc, char** argv) {
    const char* path = (argc > 1) ? argv[1] : cwd;
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

static int cat_fd(int fd) {
    char buf[512];
    ssize_t r;

    while ((r = read(fd, buf, sizeof(buf))) > 0) {
        write(1, buf, (size_t)r);
    }

    return (r < 0) ? -1 : 0;
}

static int cmd_cat(int argc, char **argv) {
    if (argc == 1) {
        return cat_fd(0);
    }

    int ret = 0;

    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            set_color(COL_ERR, 0);
            out("cat: cannot open ");
            out(argv[i]);
            out("\n");
            set_color(COL_RESET, 0);
            ret = -1;
            continue;
        }

        if (cat_fd(fd) != 0) ret = -1;
        close(fd);
    }

    return ret;
}

static int cmd_mkdir(int argc, char **argv) {
    if (argc < 2) {
        out("usage: mkdir <dir>\n");
        return -1;
    }

    if (mkdir(argv[1], 0755) != 0) {
        set_color(COL_ERR, 0);
        out("mkdir: failed\n");
        set_color(COL_RESET, 0);
        return -1;
    }

    return 0;
}

static int cmd_touch(int argc, char **argv) {
    if (argc < 2) {
        out("usage: touch <file>\n");
        return -1;
    }

    int fd = open(argv[1], O_WRONLY | O_CREAT, 0644);
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
    if (argc < 2) {
        out("usage: rm <path>\n");
        return -1;
    }

    if (unlink(argv[1]) != 0 && rmdir(argv[1]) != 0) {
        set_color(COL_ERR, 0);
        out("rm: cannot remove ");
        out(argv[1]);
        out("\n");
        set_color(COL_RESET, 0);
        return -1;
    }

    return 0;
}

static int cmd_ps(int argc, char **argv) {
    (void)argc;
    (void)argv;

    ps_entry_t ents[32];
    long n = syscall(SYS_ps, (long)ents, 32, 0, 0, 0, 0);

    if (n <= 0) {
        out("no processes\n");
        return 0;
    }

    out("PID  PPID  ST  NAME\n");

    for (long i = 0; i < n; i++) {
        put_u64((unsigned long long)ents[i].pid);
        out("    ");
        put_u64((unsigned long long)ents[i].ppid);
        out("    ");

        switch (ents[i].state) {
            case 1:
                out("R   ");
                break;
            case 2:
                out("R   ");
                break;
            case 3:
                out("S   ");
                break;
            case 4:
                out("B   ");
                break;
            case 5:
                out("Z   ");
                break;
            default:
                out("?   ");
                break;
        }

        out(ents[i].name);
        out("\n");
    }

    return 0;
}

static int cmd_mem(int argc, char **argv) {
    (void)argc;
    (void)argv;

    unsigned long long t = 0;
    unsigned long long u = 0;
    unsigned long long f = 0;

    syscall(SYS_meminfo, (long)&t, (long)&u, (long)&f, 0, 0, 0);

    out("total: ");
    put_u64(t / 1024);
    out(" KB\n");

    out("used:  ");
    put_u64(u / 1024);
    out(" KB\n");

    out("free:  ");
    put_u64(f / 1024);
    out(" KB\n");

    return 0;
}

static int cmd_uptime(int argc, char **argv) {
    (void)argc;
    (void)argv;

    unsigned long ticks = (unsigned long)syscall(SYS_gettime, 0, 0, 0, 0, 0, 0);
    unsigned long s = ticks / 1000;
    unsigned long h = s / 3600;
    unsigned long m = (s % 3600) / 60;
    unsigned long sec = s % 60;

    out("up ");

    if (h) {
        put_u64(h);
        out("h ");
    }

    if (h || m) {
        put_u64(m);
        out("m ");
    }

    put_u64(sec);
    out("s\n");

    return 0;
}

static int cmd_uname(int argc, char **argv) {
    (void)argc;
    (void)argv;

    out("UTMS7 0.2 x86_64 hybrid kernel\n");
    return 0;
}

static int is_builtin(const char *name) {
    if (strcmp(name, "help") == 0) return 1;
    if (strcmp(name, "exit") == 0) return 1;
    if (strcmp(name, "clear") == 0) return 1;
    if (strcmp(name, "echo") == 0) return 1;
    if (strcmp(name, "pwd") == 0) return 1;
    if (strcmp(name, "cd") == 0) return 1;
    if (strcmp(name, "ls") == 0) return 1;
    if (strcmp(name, "cat") == 0) return 1;
    if (strcmp(name, "mkdir") == 0) return 1;
    if (strcmp(name, "touch") == 0) return 1;
    if (strcmp(name, "rm") == 0) return 1;
    if (strcmp(name, "ps") == 0) return 1;
    if (strcmp(name, "mem") == 0) return 1;
    if (strcmp(name, "uptime") == 0) return 1;
    if (strcmp(name, "uname") == 0) return 1;
    return 0;
}

static int run_builtin(int argc, char **argv) {
    if (strcmp(argv[0], "help") == 0) return cmd_help(argc, argv);
    if (strcmp(argv[0], "exit") == 0) return cmd_exit(argc, argv);
    if (strcmp(argv[0], "clear") == 0) return cmd_clear(argc, argv);
    if (strcmp(argv[0], "echo") == 0) return cmd_echo(argc, argv);
    if (strcmp(argv[0], "pwd") == 0) return cmd_pwd(argc, argv);
    if (strcmp(argv[0], "cd") == 0) return cmd_cd(argc, argv);
    if (strcmp(argv[0], "ls") == 0) return cmd_ls(argc, argv);
    if (strcmp(argv[0], "cat") == 0) return cmd_cat(argc, argv);
    if (strcmp(argv[0], "mkdir") == 0) return cmd_mkdir(argc, argv);
    if (strcmp(argv[0], "touch") == 0) return cmd_touch(argc, argv);
    if (strcmp(argv[0], "rm") == 0) return cmd_rm(argc, argv);
    if (strcmp(argv[0], "ps") == 0) return cmd_ps(argc, argv);
    if (strcmp(argv[0], "mem") == 0) return cmd_mem(argc, argv);
    if (strcmp(argv[0], "uptime") == 0) return cmd_uptime(argc, argv);
    if (strcmp(argv[0], "uname") == 0) return cmd_uname(argc, argv);
    return -1;
}

static int run_builtin_with_redirect(int argc, char **argv, int in_fd, int out_fd) {
    int saved_in = -1;
    int saved_out = -1;

    if (in_fd != 0) {
        saved_in = dup(0);
        dup2(in_fd, 0);
        close(in_fd);
    }

    if (out_fd != 1) {
        saved_out = dup(1);
        dup2(out_fd, 1);
        dup2(out_fd, 2);
        close(out_fd);
    }

    int ret = run_builtin(argc, argv);

    if (saved_in >= 0) {
        dup2(saved_in, 0);
        close(saved_in);
    }

    if (saved_out >= 0) {
        dup2(saved_out, 1);
        dup2(saved_out, 2);
        close(saved_out);
    }

    return ret;
}

static int run_external(int argc, char **argv, int in_fd, int out_fd) {
    int pid = fork();

    if (pid == 0) {
        if (in_fd != 0) {
            dup2(in_fd, 0);
            close(in_fd);
        }

        if (out_fd != 1) {
            dup2(out_fd, 1);
            dup2(out_fd, 2);
            close(out_fd);
        }

        if (strchr(argv[0], '/')) {
            execve(argv[0], argv, NULL);
        } else {
            char path[256];

            strcpy(path, "./");
            strcat(path, argv[0]);
            execve(path, argv, NULL);

            strcpy(path, "/bin/");
            strcat(path, argv[0]);
            execve(path, argv, NULL);

            strcpy(path, "/sbin/");
            strcat(path, argv[0]);
            execve(path, argv, NULL);

            execve(argv[0], argv, NULL);
        }

        set_color(COL_ERR, 0);
        out("sh: command not found: ");
        out(argv[0]);
        out("\n");
        set_color(COL_RESET, 0);
        _exit(127);
    }

    if (pid > 0) {
        if (in_fd != 0) close(in_fd);
        if (out_fd != 1) close(out_fd);

        int status;
        waitpid(pid, &status, 0);
        return status;
    }

    if (in_fd != 0) close(in_fd);
    if (out_fd != 1) close(out_fd);

    set_color(COL_ERR, 0);
    out("sh: fork failed\n");
    set_color(COL_RESET, 0);

    return -1;
}

static int make_heredoc_file(void) {
    const char *candidates[3];
    candidates[0] = "/tmp/.heredoc";
    candidates[1] = "/.heredoc";
    candidates[2] = ".heredoc";

    for (int i = 0; i < 3; i++) {
        int fd = open(candidates[i], O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            close(fd);
            strncpy(heredoc_file, candidates[i], sizeof(heredoc_file) - 1);
            heredoc_file[sizeof(heredoc_file) - 1] = '\0';
            return 0;
        }
    }

    return -1;
}

static int collect_heredoc(const char *delim, const char *path) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;

    char hline[MAX_LINE];
    int hpos = 0;
    char c;

    out("> ");

    while (1) {
        if (read(0, &c, 1) <= 0) continue;

        if (c == '\n' || c == '\r') {
            hline[hpos] = '\0';

            if (strcmp(hline, delim) == 0) break;

            write(fd, hline, (size_t)hpos);
            write(fd, "\n", 1);

            hpos = 0;
            out("> ");
        } else if (c == '\b' || c == 0x7F) {
            if (hpos > 0) hpos--;
        } else if (c >= 32 && c <= 126) {
            if (hpos < MAX_LINE - 1) hline[hpos++] = c;
        }
    }

    close(fd);
    return 0;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    set_color(COL_RESET, 0);
    refresh_cwd();
    out("UTMS7 shell ready. Type 'help'.\n");

    char line[MAX_LINE];

    while (1) {
        print_prompt();

        int pos = 0;
        char c;

        line[0] = '\0';

        while (1) {
            if (read(0, &c, 1) <= 0) continue;

            if (c == '\b' || c == 0x7F) {
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

        char *tokens[MAX_TOKENS];
        int targc = 0;
        char *p = line;

        while (*p && targc < MAX_TOKENS - 1) {
            while (*p == ' ' || *p == '\t') p++;
            if (!*p) break;

            tokens[targc++] = p;

            while (*p && *p != ' ' && *p != '\t') p++;

            if (*p) *p++ = '\0';
        }

        tokens[targc] = NULL;

        char *cmd_argv[MAX_ARGS + 1];
        int cmd_argc = 0;

        int in_fd = 0;
        int out_fd = 1;

        int heredoc = 0;
        char *heredoc_delim = NULL;

        int parse_error = 0;

        for (int i = 0; i < targc; i++) {
            if (strcmp(tokens[i], ">") == 0) {
                if (i + 1 >= targc) {
                    parse_error = 1;
                    break;
                }

                if (out_fd > 1) close(out_fd);

                out_fd = open(tokens[++i], O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (out_fd < 0) {
                    parse_error = 1;
                    break;
                }
            } else if (strcmp(tokens[i], ">>") == 0) {
                if (i + 1 >= targc) {
                    parse_error = 1;
                    break;
                }

                if (out_fd > 1) close(out_fd);

                out_fd = open(tokens[++i], O_WRONLY | O_CREAT | O_APPEND, 0644);
                if (out_fd < 0) {
                    parse_error = 1;
                    break;
                }
            } else if (strcmp(tokens[i], "<") == 0) {
                if (i + 1 >= targc) {
                    parse_error = 1;
                    break;
                }

                if (in_fd != 0) close(in_fd);

                in_fd = open(tokens[++i], O_RDONLY);
                if (in_fd < 0) {
                    parse_error = 1;
                    break;
                }
            } else if (strcmp(tokens[i], "<<") == 0) {
                if (i + 1 >= targc) {
                    parse_error = 1;
                    break;
                }

                heredoc = 1;
                heredoc_delim = tokens[++i];
            } else {
                if (cmd_argc < MAX_ARGS) cmd_argv[cmd_argc++] = tokens[i];
            }
        }

        cmd_argv[cmd_argc] = NULL;

        if (parse_error || cmd_argc == 0) {
            if (in_fd != 0) close(in_fd);
            if (out_fd > 1) close(out_fd);

            set_color(COL_ERR, 0);
            out("sh: parse error\n");
            set_color(COL_RESET, 0);
            continue;
        }

        heredoc_file[0] = '\0';

        if (heredoc) {
            if (make_heredoc_file() != 0) {
                if (in_fd != 0) close(in_fd);
                if (out_fd > 1) close(out_fd);

                set_color(COL_ERR, 0);
                out("sh: cannot create heredoc file\n");
                set_color(COL_RESET, 0);
                continue;
            }

            if (collect_heredoc(heredoc_delim, heredoc_file) != 0) {
                if (in_fd != 0) close(in_fd);
                if (out_fd > 1) close(out_fd);
                if (heredoc_file[0]) unlink(heredoc_file);

                set_color(COL_ERR, 0);
                out("sh: heredoc failed\n");
                set_color(COL_RESET, 0);
                continue;
            }

            if (in_fd != 0) close(in_fd);

            in_fd = open(heredoc_file, O_RDONLY);
            if (in_fd < 0) {
                if (out_fd > 1) close(out_fd);
                unlink(heredoc_file);

                set_color(COL_ERR, 0);
                out("sh: cannot open heredoc file\n");
                set_color(COL_RESET, 0);
                continue;
            }
        }

        if (is_builtin(cmd_argv[0])) {
            run_builtin_with_redirect(cmd_argc, cmd_argv, in_fd, out_fd);
        } else {
            run_external(cmd_argc, cmd_argv, in_fd, out_fd);
        }

        if (heredoc_file[0]) {
            unlink(heredoc_file);
            heredoc_file[0] = '\0';
        }

        set_color(COL_RESET, 0);
    }

    return 0;
}
