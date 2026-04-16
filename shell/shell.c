// shell/shell.c
#include "../include/string.h"
#include "../drivers/vga.h"
#include "../drivers/keyboard.h"
#include "../fs/ufs.h"
#include "../kernel/memory.h"
#include "../include/path.h"
#include "../commands/fs.h"
#include "shell.h"

// Прототипы функций libc, которые используем
extern int open(const char *path, int flags, ...);
extern int close(int fd);
extern int dup(int oldfd);
extern int dup2(int oldfd, int newfd);
extern int unlink(const char *path);
extern int lseek(int fd, int offset, int whence);

static shell_command_t commands[MAX_COMMANDS];
static int cmd_count = 0;
static char history[MAX_HISTORY][MAX_LINE_LEN];
static int history_count = 0;
static int history_pos = -1;

// Структура для команды с перенаправлениями
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

void shell_unregister_command(const char *name) {
    for (int i = 0; i < cmd_count; i++) {
        if (strcmp(commands[i].name, name) == 0) {
            for (int j = i; j < cmd_count - 1; j++) commands[j] = commands[j + 1];
            cmd_count--;
            return;
        }
    }
}

// Парсинг команды с поддержкой перенаправлений и пайпов
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

// Выполнение команды с перенаправлениями
static int execute_redirected(shell_cmd_t *cmd) {
    int saved_stdin = -1, saved_stdout = -1, saved_stderr = -1;
    int new_stdin = -1, new_stdout = -1, new_stderr = -1;
    int result = -1;

    // Если команда "cat" без аргументов и есть output_file — это создание файла
    if (cmd->argc == 1 && strcmp(cmd->args[0], "cat") == 0 && cmd->output_file) {
        int fd = open(cmd->output_file, 0x41, 0644); // O_WRONLY | O_CREAT
        if (fd >= 0) close(fd);
        return 0;
    }

    // Перенаправление ввода
    if (cmd->input_file) {
        new_stdin = open(cmd->input_file, 0);
        if (new_stdin < 0) {
            shell_print("Cannot open input file: ");
            shell_print(cmd->input_file);
            shell_print("\n");
            return -1;
        }
        saved_stdin = dup(0);
        dup2(new_stdin, 0);
    }

    // Перенаправление вывода
    if (cmd->output_file) {
        int flags = 0x41; // O_WRONLY | O_CREAT
        if (cmd->append_mode) flags |= 0x400; // O_APPEND
        new_stdout = open(cmd->output_file, flags, 0644);
        if (new_stdout < 0) {
            shell_print("Cannot open output file: ");
            shell_print(cmd->output_file);
            shell_print("\n");
            goto cleanup;
        }
        saved_stdout = dup(1);
        dup2(new_stdout, 1);
    }

    // Перенаправление ошибок
    if (cmd->error_file) {
        int flags = 0x41;
        new_stderr = open(cmd->error_file, flags, 0644);
        if (new_stderr < 0) {
            shell_print("Cannot open error file: ");
            shell_print(cmd->error_file);
            shell_print("\n");
            goto cleanup;
        }
        saved_stderr = dup(2);
        dup2(new_stderr, 2);
    }

    // Выполнение команды
    for (int i = 0; i < cmd_count; i++) {
        if (strcmp(cmd->args[0], commands[i].name) == 0) {
            result = commands[i].func(cmd->argc, cmd->args);
            break;
        }
    }

    if (result == -1) {
        shell_print("unknown command: ");
        shell_print(cmd->args[0]);
        shell_print("\n");
    }

cleanup:
    // Восстановление дескрипторов
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

// Выполнение пайпа (через временный файл)
static int execute_pipe(shell_cmd_t *cmd1, shell_cmd_t *cmd2) {
    char pipe_file[] = "/tmp/pipeXXXXXX";

    // Создаём временный файл
    int pipe_fd = open(pipe_file, 0x42, 0644); // O_RDWR | O_CREAT | O_TRUNC
    if (pipe_fd < 0) {
        shell_print("Cannot create pipe file\n");
        return -1;
    }

    // Сохраняем stdout
    int saved_stdout = dup(1);

    // Перенаправляем stdout в pipe
    dup2(pipe_fd, 1);

    // Выполняем первую команду
    int result1 = execute_redirected(cmd1);

    // Восстанавливаем stdout
    dup2(saved_stdout, 1);
    close(saved_stdout);

    if (result1 == 0) {
        // Перенаправляем stdin из pipe
        int saved_stdin = dup(0);
        lseek(pipe_fd, 0, 0);
        dup2(pipe_fd, 0);

        // Выполняем вторую команду
        int result2 = execute_redirected(cmd2);

        // Восстанавливаем stdin
        dup2(saved_stdin, 0);
        close(saved_stdin);
    }

    close(pipe_fd);
    unlink(pipe_file);

    return 0;
}

char **shell_split_args(char *str, int *argc) {
    static char *argv[16];
    *argc = 0;
    int in_word = 0;
    while (*str && *argc < 16) {
        if (*str == ' ' || *str == '\t') {
            *str = '\0';
            in_word = 0;
        } else {
            if (!in_word) {
                argv[*argc] = str;
                (*argc)++;
                in_word = 1;
            }
        }
        str++;
    }
    return argv;
}

void shell_print(const char *str) { vga_write(str); }
void shell_print_num(u32 num) { vga_write_num(num); }
void shell_print_hex(u32 num) { vga_write_hex(num); }

int shell_execute(const char *cmd_line) {
    if (!cmd_line || !cmd_line[0]) return 0;

    char buf[MAX_LINE_LEN];
    strcpy(buf, cmd_line);

    // Убираем trailing newline
    int len = strlen(buf);
    if (len > 0 && buf[len-1] == '\n') buf[len-1] = '\0';

    shell_cmd_t cmd1, cmd2;
    int parse_result = parse_command(buf, &cmd1);

    if (parse_result == 1 && cmd1.pipe_to_next) {
        // Есть пайп — парсим вторую команду
        char *pipe_pos = strchr(buf, '|');
        if (pipe_pos) {
            parse_command(pipe_pos + 1, &cmd2);
            return execute_pipe(&cmd1, &cmd2);
        }
    } else if (parse_result == 2) {
        // && — выполнить если первая успешна
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
        // || — выполнить если первая не успешна
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
    const char *cwd = fs_get_current_dir();
    if (cwd && cwd[0]) {
        shell_print(cwd);
    } else {
        shell_print("/");
    }
    shell_print("> ");
}

static void clear_line(u8 input_x, u8 input_y) {
    vga_setpos(input_x, input_y);
    for (int i = 0; i < 80 - input_x; i++) {
        vga_putchar(' ');
    }
    vga_setpos(input_x, input_y);
}

int shell_run(void) {
    char line[MAX_LINE_LEN];
    int pos = 0;
    u8 cursor_x = 0, cursor_y = 0;
    int key;

    while (1) {
        print_prompt();
        vga_getpos(&cursor_x, &cursor_y);
        u8 input_x = cursor_x, input_y = cursor_y;
        pos = 0;
        history_pos = -1;
        line[0] = '\0';

        while (1) {
            if (!keyboard_data_ready()) continue;

            key = keyboard_getc();

            if (key == 0xE0) {
                if (history_pos < history_count - 1) {
                    history_pos++;
                    strcpy(line, history[history_pos]);
                    pos = strlen(line);
                    clear_line(input_x, input_y);
                    vga_setpos(input_x, input_y);
                    for (int i = 0; i < pos; i++) vga_putchar(line[i]);
                    cursor_x = input_x + pos;
                    vga_setpos(cursor_x, input_y);
                }
                continue;
            }

            if (key == 0xE1) {
                if (history_pos > 0) {
                    history_pos--;
                    strcpy(line, history[history_pos]);
                    pos = strlen(line);
                    clear_line(input_x, input_y);
                    vga_setpos(input_x, input_y);
                    for (int i = 0; i < pos; i++) vga_putchar(line[i]);
                    cursor_x = input_x + pos;
                    vga_setpos(cursor_x, input_y);
                } else if (history_pos == 0) {
                    history_pos = -1;
                    line[0] = '\0';
                    pos = 0;
                    clear_line(input_x, input_y);
                    cursor_x = input_x;
                    vga_setpos(cursor_x, input_y);
                }
                continue;
            }

            if (key == 0xE2) {
                if (cursor_x > input_x) {
                    cursor_x--;
                    vga_setpos(cursor_x, input_y);
                }
                continue;
            }

            if (key == 0xE3) {
                if (cursor_x - input_x < pos) {
                    cursor_x++;
                    vga_setpos(cursor_x, input_y);
                }
                continue;
            }

            if (key == '\t') {
                continue;
            }

            if (key == '\b' || key == 0x7F) {
                if (cursor_x > input_x) {
                    int idx = cursor_x - input_x - 1;
                    for (int i = idx; i < pos; i++) line[i] = line[i+1];
                    pos--;
                    cursor_x--;
                    vga_setpos(input_x, input_y);
                    for (int i = 0; i < pos; i++) vga_putchar(line[i]);
                    vga_putchar(' ');
                    vga_setpos(cursor_x, input_y);
                }
                continue;
            }

            if (key == '\n' || key == '\r') {
                line[pos] = '\0';
                vga_putchar('\n');
                if (pos > 0) {
                    add_to_history(line);
                    shell_execute(line);
                }
                break;
            }

            if (key >= 32 && key <= 126) {
                if (pos < MAX_LINE_LEN - 1) {
                    int idx = cursor_x - input_x;
                    for (int i = pos; i > idx; i--) line[i] = line[i-1];
                    line[idx] = key;
                    pos++;
                    cursor_x++;
                    vga_setpos(input_x, input_y);
                    for (int i = 0; i < pos; i++) vga_putchar(line[i]);
                    vga_setpos(cursor_x, input_y);
                }
                continue;
            }
        }
    }
    return 0;
}
