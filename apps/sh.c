// apps/sh.c
#include "../lib/libc.h"

#define MAX_COMMANDS 64
#define MAX_LINE_LEN 512
#define MAX_HISTORY 16

typedef struct {
    char name[32];
    int (*func)(int argc, char** argv);
    char desc[64];
} shell_command_t;

static shell_command_t commands[MAX_COMMANDS];
static int cmd_count = 0;
static char history[MAX_HISTORY][MAX_LINE_LEN];
static int history_count = 0;
static int history_pos = -1;

typedef struct {
    char *args[16];
    int argc;
    char *input_file;
    char *output_file;
    int append_mode;
    char *error_file;
    int background;
    int pipe_to_next;
} shell_cmd_t;

void print(const char *s) {
    write(1, s, strlen(s));
}

int shell_init(void) {
    cmd_count = 0;
    for (int i = 0; i < MAX_COMMANDS; i++) {
        commands[i].name[0] = '\0';
        commands[i].func = NULL;
        commands[i].desc[0] = '\0';
    }
    for (int i = 0; i < MAX_HISTORY; i++) history[i][0] = '\0';
    history_count = 0;
    history_pos = -1;
    return 0;
}

static void add_to_history(const char *line) {
    if (!line || !line[0]) return;
    if (history_count > 0 && strcmp(history[0], line) == 0) return;
    for (int i = MAX_HISTORY - 1; i > 0; i--) strcpy(history[i], history[i-1]);
    strcpy(history[0], line);
    if (history_count < MAX_HISTORY) history_count++;
}

int shell_register_command(const char *name, int (*func)(int, char**), const char *desc) {
    if (!name || !func || cmd_count >= MAX_COMMANDS) return -1;
    for (int i = 0; i < cmd_count; i++) {
        if (strcmp(commands[i].name, name) == 0) return -1;
    }
    strcpy(commands[cmd_count].name, name);
    commands[cmd_count].func = func;
    if (desc) strcpy(commands[cmd_count].desc, desc);
    cmd_count++;
    return 0;
}

static int parse_command(char *line, shell_cmd_t *cmd) {
    memset(cmd, 0, sizeof(shell_cmd_t));

    char *p = line;
    int arg_idx = 0;
    int in_quote = 0;
    char quote_char = 0;
    char *arg_start = p;

    while (*p && arg_idx < 15) {
        if (!in_quote && (*p == '"' || *p == '\'')) {
            in_quote = 1;
            quote_char = *p;
            arg_start = p + 1;
            p++;
            continue;
        }

        if (in_quote && *p == quote_char) {
            in_quote = 0;
            *p = '\0';
            cmd->args[arg_idx++] = arg_start;
            p++;
            continue;
        }

        if (!in_quote && (*p == ' ' || *p == '\t')) {
            if (arg_start != p) {
                *p = '\0';
                cmd->args[arg_idx++] = arg_start;
            }
            p++;
            arg_start = p;
            continue;
        }

        if (!in_quote && *p == '>') {
            if (arg_start != p) {
                *p = '\0';
                cmd->args[arg_idx++] = arg_start;
            }
            p++;
            if (*p == '>') {
                cmd->append_mode = 1;
                p++;
            }
            while (*p == ' ' || *p == '\t') p++;
            cmd->output_file = p;
            while (*p && *p != ' ' && *p != '\t' && *p != '>' && *p != '<' && *p != '|') p++;
            if (*p) {
                *p = '\0';
                p++;
            }
            continue;
        }

        if (!in_quote && *p == '<') {
            if (arg_start != p) {
                *p = '\0';
                cmd->args[arg_idx++] = arg_start;
            }
            p++;
            while (*p == ' ' || *p == '\t') p++;
            cmd->input_file = p;
            while (*p && *p != ' ' && *p != '\t' && *p != '>' && *p != '<' && *p != '|') p++;
            if (*p) {
                *p = '\0';
                p++;
            }
            continue;
        }

        if (!in_quote && *p == '2' && *(p+1) == '>') {
            if (arg_start != p) {
                *p = '\0';
                cmd->args[arg_idx++] = arg_start;
            }
            p += 2;
            if (*p == '>') {
                p++;
            }
            while (*p == ' ' || *p == '\t') p++;
            cmd->error_file = p;
            while (*p && *p != ' ' && *p != '\t' && *p != '>' && *p != '<' && *p != '|') p++;
            if (*p) {
                *p = '\0';
                p++;
            }
            continue;
        }

        if (!in_quote && *p == '|') {
            if (arg_start != p) {
                *p = '\0';
                cmd->args[arg_idx++] = arg_start;
            }
            cmd->pipe_to_next = 1;
            p++;
            return 1;
        }

        if (!in_quote && *p == '&' && *(p+1) == '&') {
            if (arg_start != p) {
                *p = '\0';
                cmd->args[arg_idx++] = arg_start;
            }
            p += 2;
            return 2;
        }

        if (!in_quote && *p == '|' && *(p+1) == '|') {
            if (arg_start != p) {
                *p = '\0';
                cmd->args[arg_idx++] = arg_start;
            }
            p += 2;
            return 3;
        }

        p++;
    }

    if (arg_start != p && arg_idx < 15) {
        cmd->args[arg_idx++] = arg_start;
    }
    cmd->argc = arg_idx;

    return 0;
}

static int execute_redirected(shell_cmd_t *cmd) {
    int saved_stdin = -1, saved_stdout = -1, saved_stderr = -1;
    int new_stdin = -1, new_stdout = -1, new_stderr = -1;
    int result = -1;

    if (cmd->argc == 1 && strcmp(cmd->args[0], "cat") == 0 && cmd->output_file) {
        int fd = open(cmd->output_file, 0x41, 0644);
        if (fd >= 0) close(fd);
        return 0;
    }

    if (cmd->input_file) {
        new_stdin = open(cmd->input_file, 0);
        if (new_stdin < 0) {
            print("Cannot open input file: ");
            print(cmd->input_file);
            print("\n");
            return -1;
        }
        saved_stdin = dup(0);
        dup2(new_stdin, 0);
    }

    if (cmd->output_file) {
        int flags = 0x41;
        if (cmd->append_mode) flags |= 0x400;
        new_stdout = open(cmd->output_file, flags, 0644);
        if (new_stdout < 0) {
            print("Cannot open output file: ");
            print(cmd->output_file);
            print("\n");
            goto cleanup;
        }
        saved_stdout = dup(1);
        dup2(new_stdout, 1);
    }

    if (cmd->error_file) {
        int flags = 0x41;
        new_stderr = open(cmd->error_file, flags, 0644);
        if (new_stderr < 0) {
            print("Cannot open error file: ");
            print(cmd->error_file);
            print("\n");
            goto cleanup;
        }
        saved_stderr = dup(2);
        dup2(new_stderr, 2);
    }

    for (int i = 0; i < cmd_count; i++) {
        if (strcmp(cmd->args[0], commands[i].name) == 0) {
            result = commands[i].func(cmd->argc, cmd->args);
            break;
        }
    }

    if (result == -1) {
        print("unknown command: ");
        print(cmd->args[0]);
        print("\n");
    }

cleanup:
    if (saved_stdin >= 0) {
        dup2(saved_stdin, 0);
        close(saved_stdin);
        if (new_stdin >= 0) close(new_stdin);
    }
    if (saved_stdout >= 0) {
        dup2(saved_stdout, 1);
        close(saved_stdout);
        if (new_stdout >= 0) close(new_stdout);
    }
    if (saved_stderr >= 0) {
        dup2(saved_stderr, 2);
        close(saved_stderr);
        if (new_stderr >= 0) close(new_stderr);
    }

    return result;
}

static int execute_pipe(shell_cmd_t *cmd1, shell_cmd_t *cmd2) {
    char pipe_file[] = "/tmp/pipeXXXXXX";

    int pipe_fd = open(pipe_file, 0x42, 0644);
    if (pipe_fd < 0) {
        print("Cannot create pipe file\n");
        return -1;
    }

    int saved_stdout = dup(1);

    dup2(pipe_fd, 1);

    int result1 = execute_redirected(cmd1);

    dup2(saved_stdout, 1);
    close(saved_stdout);

    if (result1 == 0) {
        int saved_stdin = dup(0);
        lseek(pipe_fd, 0, 0);
        dup2(pipe_fd, 0);

        int result2 = execute_redirected(cmd2);

        dup2(saved_stdin, 0);
        close(saved_stdin);
    }

    close(pipe_fd);
    unlink(pipe_file);

    return 0;
}

int shell_execute(const char *cmd_line) {
    if (!cmd_line || !cmd_line[0]) return 0;

    char buf[MAX_LINE_LEN];
    strcpy(buf, cmd_line);

    int len = strlen(buf);
    if (len > 0 && buf[len-1] == '\n') buf[len-1] = '\0';

    shell_cmd_t cmd1, cmd2;
    int parse_result = parse_command(buf, &cmd1);

    if (parse_result == 1 && cmd1.pipe_to_next) {
        char *pipe_pos = strchr(buf, '|');
        if (pipe_pos) {
            parse_command(pipe_pos + 1, &cmd2);
            return execute_pipe(&cmd1, &cmd2);
        }
    } else if (parse_result == 2) {
        int result = execute_redirected(&cmd1);
        if (result == 0) {
            char *and_pos = strstr(buf, "&&");
            if (and_pos) {
                parse_command(and_pos + 2, &cmd2);
                return execute_redirected(&cmd2);
            }
        }
        return result;
    } else if (parse_result == 3) {
        int result = execute_redirected(&cmd1);
        if (result != 0) {
            char *or_pos = strstr(buf, "||");
            if (or_pos) {
                parse_command(or_pos + 2, &cmd2);
                return execute_redirected(&cmd2);
            }
        }
        return result;
    }

    return execute_redirected(&cmd1);
}

static void print_prompt(void) {
    print("/> ");
}

int main(void) {
    shell_init();
    char line[MAX_LINE_LEN];
    int pos = 0;
    char c;

    while (1) {
        print_prompt();
        pos = 0;
        line[0] = '\0';

        while (1) {
            read(0, &c, 1);

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
                if (pos > 0) {
                    add_to_history(line);
                    shell_execute(line);
                }
                break;
            }

            if (c >= 32 && c <= 126) {
                if (pos < MAX_LINE_LEN - 1) {
                    line[pos++] = c;
                    write(1, &c, 1);
                }
            }
        }
    }
    return 0;
}
