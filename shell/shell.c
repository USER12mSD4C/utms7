#include "../include/shell_api.h"
#include "../include/string.h"
#include "../drivers/vga.h"
#include "../drivers/keyboard.h"
#include "../kernel/kapi.h"
#include "../kernel/memory.h"

#define MAX_COMMANDS 64
#define MAX_LINE_LEN 512

static shell_command_t commands[MAX_COMMANDS];
static int cmd_count = 0;
static char current_dir[256] = "/";

void shell_init(void) {
    cmd_count = 0;
    for (int i = 0; i < MAX_COMMANDS; i++) {
        commands[i].name[0] = '\0';
        commands[i].func = NULL;
    }
}

int shell_register_command(const char* name, int (*func)(int argc, char** argv), const char* desc) {
    if (!name || !func || cmd_count >= MAX_COMMANDS) return -1;
    
    // Проверяем уникальность
    for (int i = 0; i < cmd_count; i++) {
        if (strcmp(commands[i].name, name) == 0) return -1;
    }
    
    strcpy(commands[cmd_count].name, name);
    commands[cmd_count].func = func;
    if (desc) strcpy(commands[cmd_count].desc, desc);
    cmd_count++;
    
    return 0;
}

int shell_unregister_command(const char* name) {
    for (int i = 0; i < cmd_count; i++) {
        if (strcmp(commands[i].name, name) == 0) {
            // Сдвигаем остальные команды
            for (int j = i; j < cmd_count - 1; j++) {
                commands[j] = commands[j + 1];
            }
            cmd_count--;
            return 0;
        }
    }
    return -1;
}

char** shell_split_args(char* str, int* argc) {
    static char* argv[CMD_MAX_ARGS];
    *argc = 0;
    int in_word = 0;
    
    while (*str && *argc < CMD_MAX_ARGS) {
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

void shell_print(const char* str) {
    vga_write(str);
}

void shell_print_num(u32 num) {
    char buf[16];
    int i = 0;
    
    if (num == 0) {
        vga_putchar('0');
        return;
    }
    
    while (num > 0) {
        buf[i++] = '0' + (num % 10);
        num /= 10;
    }
    
    while (i > 0) {
        vga_putchar(buf[--i]);
    }
}

void shell_print_hex(u32 num) {
    char hex[] = "0123456789ABCDEF";
    vga_write("0x");
    vga_putchar(hex[(num >> 28) & 0xF]);
    vga_putchar(hex[(num >> 24) & 0xF]);
    vga_putchar(hex[(num >> 20) & 0xF]);
    vga_putchar(hex[(num >> 16) & 0xF]);
    vga_putchar(hex[(num >> 12) & 0xF]);
    vga_putchar(hex[(num >> 8) & 0xF]);
    vga_putchar(hex[(num >> 4) & 0xF]);
    vga_putchar(hex[num & 0xF]);
}

int shell_execute(const char* cmd_line) {
    if (!cmd_line || !cmd_line[0]) return 0;
    
    char buf[MAX_LINE_LEN];
    strcpy(buf, cmd_line);
    
    int argc;
    char** argv = shell_split_args(buf, &argc);
    
    if (argc == 0) return 0;
    
    for (int i = 0; i < cmd_count; i++) {
        if (strcmp(argv[0], commands[i].name) == 0) {
            return commands[i].func(argc, argv);
        }
    }
    
    shell_print("Unknown command: ");
    shell_print(argv[0]);
    shell_print("\n");
    return -1;
}

void shell_run(void) {
    char line[MAX_LINE_LEN];
    int pos = 0;
    
    while (1) {
        shell_print(current_dir);
        shell_print("> ");
        
        pos = 0;
        while (1) {
            if (keyboard_data_ready()) {
                char c = keyboard_getc();
                
                if (c == '\n') {
                    line[pos] = '\0';
                    shell_print("\n");
                    if (pos > 0) {
                        shell_execute(line);
                    }
                    break;
                } else if (c == '\b') {
                    if (pos > 0) {
                        pos--;
                        shell_print("\b \b");
                    }
                } else {
                    line[pos++] = c;
                    shell_print(&c);
                }
            }
        }
    }
}
