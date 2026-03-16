#include "../include/shell_api.h"
#include "../include/string.h"
#include "../drivers/vga.h"
#include "../drivers/keyboard.h"
#include "../kernel/kapi.h"
#include "../kernel/memory.h"
#include "../kernel/sched.h"
#include "../commands/fs.h"
#include "../fs/ufs.h"

#define MAX_COMMANDS 64
#define MAX_LINE_LEN 512
#define MAX_HISTORY 16

shell_command_t commands[MAX_COMMANDS];
int cmd_count = 0;
static char current_dir[256] = "/";

// История команд
static char history[MAX_HISTORY][MAX_LINE_LEN];
static int history_count = 0;
static int history_pos = -1;

void shell_init(void) {
    cmd_count = 0;
    for (int i = 0; i < MAX_COMMANDS; i++) {
        commands[i].name[0] = '\0';
        commands[i].func = NULL;
    }
    
    for (int i = 0; i < MAX_HISTORY; i++) {
        history[i][0] = '\0';
    }
    history_count = 0;
    history_pos = -1;
}

static void add_to_history(const char* line) {
    if (!line || line[0] == '\0') return;
    
    if (history_count > 0 && strcmp(history[0], line) == 0) return;
    
    for (int i = MAX_HISTORY - 1; i > 0; i--) {
        strcpy(history[i], history[i-1]);
    }
    
    strcpy(history[0], line);
    if (history_count < MAX_HISTORY) history_count++;
}

int shell_register_command(const char* name, int (*func)(int argc, char** argv), const char* desc) {
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

int shell_unregister_command(const char* name) {
    for (int i = 0; i < cmd_count; i++) {
        if (strcmp(commands[i].name, name) == 0) {
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

// Дополнение команд
static int complete_command(const char* prefix, char* buffer, int bufsize) {
    int matches = 0;
    int first_match = -1;
    
    for (int i = 0; i < cmd_count; i++) {
        if (strncmp(commands[i].name, prefix, strlen(prefix)) == 0) {
            if (matches == 0) {
                first_match = i;
                strcpy(buffer, commands[i].name);
            }
            matches++;
        }
    }
    
    if (matches == 1) {
        return 1;
    }
    
    if (matches > 1) {
        vga_putchar('\n');
        for (int i = 0; i < cmd_count; i++) {
            if (strncmp(commands[i].name, prefix, strlen(prefix)) == 0) {
                shell_print("  ");
                shell_print(commands[i].name);
                vga_putchar('\n');
            }
        }
    }
    
    return matches;
}

// Дополнение имён файлов
static int complete_filename(const char* prefix, char* buffer, int bufsize) {
    FSNode* entries;
    u32 count;
    int matches = 0;
    int first_match = -1;
    
    if (ufs_readdir(current_dir, &entries, &count) != 0) {
        return 0;
    }
    
    for (u32 i = 0; i < count; i++) {
        if (strncmp(entries[i].name, prefix, strlen(prefix)) == 0) {
            if (matches == 0) {
                first_match = i;
                strcpy(buffer, entries[i].name);
            }
            matches++;
        }
    }
    
    if (matches == 1) {
        kfree(entries);
        return 1;
    }
    
    if (matches > 1) {
        vga_putchar('\n');
        for (u32 i = 0; i < count; i++) {
            if (strncmp(entries[i].name, prefix, strlen(prefix)) == 0) {
                if (entries[i].is_dir) {
                    shell_print("  ");
                    shell_print(entries[i].name);
                    shell_print("/\n");
                } else {
                    shell_print("  ");
                    shell_print(entries[i].name);
                    vga_putchar('\n');
                }
            }
        }
    }
    
    kfree(entries);
    return matches;
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
    
    shell_print("unknown command: ");
    shell_print(argv[0]);
    vga_putchar('\n');
    return -1;
}

static void print_prompt(void) {
    shell_print(fs_get_current_dir());
    shell_print("> ");
}

static void redraw_line(const char* line, int pos, u8 input_x, u8 input_y, u8* cursor_x) {
    vga_setpos(input_x, input_y);
    for (int i = 0; i < 80 - input_x; i++) {
        vga_putchar(' ');
    }
    
    vga_setpos(input_x, input_y);
    for (int i = 0; i < pos; i++) {
        vga_putchar(line[i]);
    }
    
    *cursor_x = input_x + pos;
    vga_setpos(*cursor_x, input_y);
}

void shell_run(void) {
    char line[MAX_LINE_LEN];
    int pos = 0;
    u8 cursor_x = 0;
    u8 cursor_y = 0;
    
    while (1) {
        print_prompt();
        
        vga_getpos(&cursor_x, &cursor_y);
        u8 input_x = cursor_x;
        u8 input_y = cursor_y;
        
        pos = 0;
        history_pos = -1;
        line[0] = '\0';
        
        while (1) {
            if (!keyboard_data_ready()) continue;
            
            u8 k = keyboard_getc();
            int mods = keyboard_get_modifiers();
            
            // Стрелки
            if (k == 0xE2) { // Left
                if (cursor_x > input_x) {
                    cursor_x--;
                    vga_setpos(cursor_x, input_y);
                }
                continue;
            }
            if (k == 0xE3) { // Right
                if (cursor_x - input_x < pos) {
                    cursor_x++;
                    vga_setpos(cursor_x, input_y);
                }
                continue;
            }
            if (k == 0xE0) { // Up
                if (history_pos < history_count - 1) {
                    history_pos++;
                    strcpy(line, history[history_pos]);
                    pos = strlen(line);
                    redraw_line(line, pos, input_x, input_y, &cursor_x);
                }
                continue;
            }
            if (k == 0xE1) { // Down
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
            
            // Tab
            if (k == '\t') {
                if (pos > 0) {
                    char completion[256];
                    int result;
                    
                    // Определяем, что дополнять
                    int is_first_word = 1;
                    for (int i = 0; i < pos; i++) {
                        if (line[i] == ' ') {
                            is_first_word = 0;
                            break;
                        }
                    }
                    
                    if (is_first_word) {
                        result = complete_command(line, completion, sizeof(completion));
                    } else {
                        result = complete_filename(line, completion, sizeof(completion));
                    }
                    
                    if (result == 1) {
                        strcpy(line, completion);
                        pos = strlen(line);
                        redraw_line(line, pos, input_x, input_y, &cursor_x);
                    } else if (result > 1) {
                        // Список уже выведен, печатаем промпт и текущую строку заново
                        vga_setpos(0, input_y + 1);
                        print_prompt();
                        for (int i = 0; i < pos; i++) {
                            vga_putchar(line[i]);
                        }
                        vga_getpos(&cursor_x, &cursor_y);
                        input_x = cursor_x - pos;
                        input_y = cursor_y;
                    }
                }
                continue;
            }
            
            // Backspace
            if (k == '\b') {
                if (cursor_x > input_x) {
                    int idx = cursor_x - input_x - 1;
                    
                    for (int i = idx; i < pos; i++) {
                        line[i] = line[i+1];
                    }
                    pos--;
                    cursor_x--;
                    
                    redraw_line(line, pos, input_x, input_y, &cursor_x);
                }
                continue;
            }
            
            // Enter
            if (k == '\n') {
                line[pos] = '\0';
                vga_putchar('\n');
                if (pos > 0) {
                    add_to_history(line);
                    shell_execute(line);
                }
                break;
            }
            
            // Обычные символы
            if (k >= 32 && k <= 126 && !(mods & KEY_MOD_CTRL)) {
                if (pos < MAX_LINE_LEN - 1) {
                    int idx = cursor_x - input_x;
                    
                    for (int i = pos; i > idx; i--) {
                        line[i] = line[i-1];
                    }
                    line[idx] = k;
                    pos++;
                    cursor_x++;
                    
                    // Перерисовываем от позиции вставки
                    vga_setpos(cursor_x - 1, input_y);
                    for (int i = idx; i < pos; i++) {
                        vga_putchar(line[i]);
                    }
                    
                    vga_setpos(cursor_x, input_y);
                }
                continue;
            }
        }
    }
}
