#include "../include/string.h"
#include "../drivers/vga.h"
#include "../drivers/keyboard.h"
#include "../fs/ufs.h"
#include "../kernel/memory.h"
#include "../include/path.h"
#include "../commands/fs.h"
#include "shell.h"

#define MAX_COMMANDS 64
#define MAX_LINE_LEN 512
#define MAX_HISTORY 16

static shell_command_t commands[MAX_COMMANDS];
static int cmd_count = 0;
static char history[MAX_HISTORY][MAX_LINE_LEN];
static int history_count = 0;
static int history_pos = -1;

void shell_init(void) {
    cmd_count = 0;
    for (int i = 0; i < MAX_COMMANDS; i++) {
        commands[i].name[0] = '\0';
        commands[i].func = NULL;
        commands[i].desc[0] = '\0';
    }
    for (int i = 0; i < MAX_HISTORY; i++) history[i][0] = '\0';
    history_count = 0;
    history_pos = -1;
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
    int argc;
    char **argv = shell_split_args(buf, &argc);
    if (argc == 0) return 0;
    for (int i = 0; i < cmd_count; i++) {
        if (strcmp(argv[0], commands[i].name) == 0) {
            return commands[i].func(argc, argv);
        }
    }
    shell_print("unknown command: ");
    shell_print(argv[0]);
    vga_putchar('\n');
    return -1;
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

static void redraw_line(const char *line, int pos, u8 input_x, u8 input_y, u8 *cursor_x) {
    vga_setpos(input_x, input_y);
    for (int i = 0; i < 80 - input_x; i++) vga_putchar(' ');
    vga_setpos(input_x, input_y);
    for (int i = 0; i < pos; i++) vga_putchar(line[i]);
    *cursor_x = input_x + pos;
    vga_setpos(*cursor_x, input_y);
}

void shell_run(void) {
    char line[MAX_LINE_LEN];
    int pos = 0;
    u8 cursor_x = 0, cursor_y = 0;
    
    while (1) {
        print_prompt();
        vga_getpos(&cursor_x, &cursor_y);
        u8 input_x = cursor_x, input_y = cursor_y;
        pos = 0;
        history_pos = -1;
        line[0] = '\0';
        
        while (1) {
            if (!keyboard_data_ready()) continue;
            
            u8 k = keyboard_getc();
            
            if (k == 0xE0) {
                if (history_pos < history_count - 1) {
                    history_pos++;
                    strcpy(line, history[history_pos]);
                    pos = strlen(line);
                    redraw_line(line, pos, input_x, input_y, &cursor_x);
                }
                continue;
            }
            
            if (k == 0xE1) {
                if (history_pos >= 0) {
                    history_pos--;
                    if (history_pos >= 0) {
                        strcpy(line, history[history_pos]);
                    } else {
                        line[0] = '\0';
                    }
                    pos = strlen(line);
                    redraw_line(line, pos, input_x, input_y, &cursor_x);
                }
                continue;
            }
            
            if (k == 0xE2) {
                if (cursor_x > input_x) {
                    cursor_x--;
                    vga_setpos(cursor_x, input_y);
                }
                continue;
            }
            
            if (k == 0xE3) {
                if (cursor_x - input_x < pos) {
                    cursor_x++;
                    vga_setpos(cursor_x, input_y);
                }
                continue;
            }
            
            if (k == '\t') {
                continue;
            }
            
            if (k == '\b' || k == 0x7F) {
                if (cursor_x > input_x) {
                    int idx = cursor_x - input_x - 1;
                    for (int i = idx; i < pos; i++) line[i] = line[i+1];
                    pos--;
                    cursor_x--;
                    redraw_line(line, pos, input_x, input_y, &cursor_x);
                }
                continue;
            }
            
            if (k == '\n' || k == '\r') {
                line[pos] = '\0';
                vga_putchar('\n');
                if (pos > 0) {
                    add_to_history(line);
                    shell_execute(line);
                }
                break;
            }
            
            if (k >= 32 && k <= 126) {
                if (pos < MAX_LINE_LEN - 1) {
                    int idx = cursor_x - input_x;
                    for (int i = pos; i > idx; i--) line[i] = line[i-1];
                    line[idx] = k;
                    pos++;
                    cursor_x++;
                    vga_setpos(cursor_x - 1, input_y);
                    for (int i = idx; i < pos; i++) vga_putchar(line[i]);
                    vga_setpos(cursor_x, input_y);
                }
                continue;
            }
        }
    }
}
