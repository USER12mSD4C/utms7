// shell/uss.c
#include "../include/string.h"
#include "../drivers/vga.h"
#include "../drivers/keyboard.h"
#include "../fs/ufs.h"
#include "../kernel/memory.h"
#include "../include/syscall.h"
#include "../include/udisk.h"
#include "uss.h"

// Это не ограничения, а начальные размеры (будут расширяться)
#define USS_INIT_VARS 64
#define USS_INIT_STACK 32
#define USS_MAX_LINE 8192

typedef enum {
    VAL_NUMBER,
    VAL_STRING,
    VAL_ARRAY
} ValueType;

typedef struct {
    char name[64];
    ValueType type;
    union {
        u32 num_val;
        char* str_val;
        struct {
            void* data;
            u32 len;
            u32 elem_size;
        } array;
    } data;
} USSVar;

typedef struct {
    USSVar* vars;           // динамический массив
    int var_count;
    int var_capacity;
    
    u32* return_stack;      // динамический стек
    int return_count;
    int return_capacity;
    
    char* script_data;
    int line_num;
    int position;
    int running;
} USS;

static USS uss;

// ==================== ПРОТОТИПЫ ====================

static u32 uss_eval_primary(char** expr);
static u32 uss_eval_expression(char** expr);
static void uss_execute_line(char* line);
static void uss_print(const char *s);
static void uss_print_num(u32 n);
static void uss_print_str(const char* s);
static void uss_error(const char* msg);
static int uss_find_var(const char* name);
static void uss_set_var_num(const char* name, u32 value);
static void uss_set_var_str(const char* name, const char* value);
static u32 uss_get_var_num(const char* name);
static char* uss_get_var_str(const char* name);
static void skip_spaces(char** p);
static u32 parse_number(char** p);
static char* parse_ident(char** p);
static char* parse_string(char** p);

// ==================== ДИНАМИЧЕСКОЕ УПРАВЛЕНИЕ ====================

static void uss_ensure_var_capacity(int needed) {
    if (needed <= uss.var_capacity) return;
    
    int new_capacity = uss.var_capacity * 2;
    if (new_capacity < needed) new_capacity = needed + 64;
    
    USSVar* new_vars = kmalloc(new_capacity * sizeof(USSVar));
    if (!new_vars) {
        uss_error("out of memory for variables");
        return;
    }
    
    if (uss.vars) {
        memcpy(new_vars, uss.vars, uss.var_count * sizeof(USSVar));
        kfree(uss.vars);
    }
    
    uss.vars = new_vars;
    uss.var_capacity = new_capacity;
}

static void uss_ensure_stack_capacity(int needed) {
    if (needed <= uss.return_capacity) return;
    
    int new_capacity = uss.return_capacity * 2;
    if (new_capacity < needed) new_capacity = needed + 32;
    
    u32* new_stack = kmalloc(new_capacity * sizeof(u32));
    if (!new_stack) {
        uss_error("out of memory for stack");
        return;
    }
    
    if (uss.return_stack) {
        memcpy(new_stack, uss.return_stack, uss.return_count * sizeof(u32));
        kfree(uss.return_stack);
    }
    
    uss.return_stack = new_stack;
    uss.return_capacity = new_capacity;
}

// ==================== БАЗОВЫЕ ФУНКЦИИ ====================

static void uss_print(const char *s) {
    vga_write(s);
}

static void uss_print_num(u32 n) {
    vga_write_num(n);
}

static void uss_print_str(const char* s) {
    vga_write(s);
}

static void uss_error(const char* msg) {
    uss_print("USS error at line ");
    uss_print_num(uss.line_num);
    uss_print(": ");
    uss_print(msg);
    uss_print("\n");
    uss.running = 0;
}

// ==================== РАБОТА С ПЕРЕМЕННЫМИ ====================

static int uss_find_var(const char* name) {
    for (int i = 0; i < uss.var_count; i++) {
        if (strcmp(uss.vars[i].name, name) == 0) return i;
    }
    return -1;
}

static void uss_set_var_num(const char* name, u32 value) {
    int idx = uss_find_var(name);
    if (idx == -1) {
        uss_ensure_var_capacity(uss.var_count + 1);
        idx = uss.var_count++;
        strcpy(uss.vars[idx].name, name);
        uss.vars[idx].type = VAL_NUMBER;
        uss.vars[idx].data.str_val = NULL;
    } else {
        if (uss.vars[idx].type == VAL_STRING && uss.vars[idx].data.str_val) {
            kfree(uss.vars[idx].data.str_val);
            uss.vars[idx].data.str_val = NULL;
        }
    }
    
    uss.vars[idx].type = VAL_NUMBER;
    uss.vars[idx].data.num_val = value;
}

static void uss_set_var_str(const char* name, const char* value) {
    int idx = uss_find_var(name);
    if (idx == -1) {
        uss_ensure_var_capacity(uss.var_count + 1);
        idx = uss.var_count++;
        strcpy(uss.vars[idx].name, name);
        uss.vars[idx].type = VAL_STRING;
        uss.vars[idx].data.str_val = NULL;
    } else {
        if (uss.vars[idx].type == VAL_STRING && uss.vars[idx].data.str_val) {
            kfree(uss.vars[idx].data.str_val);
        }
    }
    
    int len = strlen(value);
    uss.vars[idx].data.str_val = kmalloc(len + 1);
    if (uss.vars[idx].data.str_val) {
        strcpy(uss.vars[idx].data.str_val, value);
        uss.vars[idx].type = VAL_STRING;
    }
}

static u32 uss_get_var_num(const char* name) {
    int idx = uss_find_var(name);
    if (idx == -1) return 0;
    
    if (uss.vars[idx].type == VAL_NUMBER) {
        return uss.vars[idx].data.num_val;
    }
    return 0;
}

static char* uss_get_var_str(const char* name) {
    int idx = uss_find_var(name);
    if (idx == -1) return NULL;
    
    if (uss.vars[idx].type == VAL_STRING) {
        return uss.vars[idx].data.str_val;
    }
    return NULL;
}

// ==================== ПАРСЕР ====================

static void skip_spaces(char** p) {
    while (**p == ' ' || **p == '\t') (*p)++;
}

static u32 parse_number(char** p) {
    u32 val = 0;
    while (**p >= '0' && **p <= '9') {
        val = val * 10 + (**p - '0');
        (*p)++;
    }
    return val;
}

static char* parse_ident(char** p) {
    static char ident[64];
    int i = 0;
    
    if (!((**p >= 'a' && **p <= 'z') || (**p >= 'A' && **p <= 'Z') || **p == '_')) {
        return NULL;
    }
    
    while ((**p >= 'a' && **p <= 'z') || (**p >= 'A' && **p <= 'Z') || 
           (**p >= '0' && **p <= '9') || **p == '_') {
        if (i < 63) ident[i++] = **p;
        (*p)++;
    }
    ident[i] = '\0';
    return ident;
}

static char* parse_string(char** p) {
    static char str[USS_MAX_LINE];
    int i = 0;
    
    if (**p != '"') return NULL;
    (*p)++;
    
    while (**p && **p != '"') {
        if (**p == '\\') {
            (*p)++;
            switch (**p) {
                case 'n': str[i++] = '\n'; break;
                case 't': str[i++] = '\t'; break;
                case 'r': str[i++] = '\r'; break;
                case '"': str[i++] = '"'; break;
                case '\\': str[i++] = '\\'; break;
                default: str[i++] = **p; break;
            }
        } else {
            str[i++] = **p;
        }
        (*p)++;
    }
    if (**p == '"') (*p)++;
    str[i] = '\0';
    return str;
}

// ==================== ВЫЧИСЛЕНИЕ ВЫРАЖЕНИЙ ====================

static u32 uss_eval_primary(char** expr) {
    skip_spaces(expr);
    
    if (**expr >= '0' && **expr <= '9') {
        return parse_number(expr);
    }
    
    if (**expr == '"') {
        parse_string(expr);
        return 0;
    }
    
    if (**expr == '(') {
        (*expr)++;
        u32 val = uss_eval_expression(expr);
        skip_spaces(expr);
        if (**expr == ')') (*expr)++;
        return val;
    }
    
    char* ident = parse_ident(expr);
    if (ident) {
        return uss_get_var_num(ident);
    }
    
    return 0;
}

static u32 uss_eval_expression(char** expr) {
    u32 left = uss_eval_primary(expr);
    
    while (1) {
        skip_spaces(expr);
        char op = **expr;
        
        switch (op) {
            case '+':
                (*expr)++;
                left = left + uss_eval_primary(expr);
                break;
            case '-':
                (*expr)++;
                left = left - uss_eval_primary(expr);
                break;
            case '*':
                (*expr)++;
                left = left * uss_eval_primary(expr);
                break;
            case '/':
                (*expr)++;
                left = left / uss_eval_primary(expr);
                break;
            case '%':
                (*expr)++;
                left = left % uss_eval_primary(expr);
                break;
            case '=':
                if (*(*expr+1) == '=') {
                    (*expr) += 2;
                    left = (left == uss_eval_primary(expr)) ? 1 : 0;
                } else {
                    return left;
                }
                break;
            case '!':
                if (*(*expr+1) == '=') {
                    (*expr) += 2;
                    left = (left != uss_eval_primary(expr)) ? 1 : 0;
                } else {
                    return left;
                }
                break;
            case '<':
                if (*(*expr+1) == '=') {
                    (*expr) += 2;
                    left = (left <= uss_eval_primary(expr)) ? 1 : 0;
                } else {
                    (*expr)++;
                    left = (left < uss_eval_primary(expr)) ? 1 : 0;
                }
                break;
            case '>':
                if (*(*expr+1) == '=') {
                    (*expr) += 2;
                    left = (left >= uss_eval_primary(expr)) ? 1 : 0;
                } else {
                    (*expr)++;
                    left = (left > uss_eval_primary(expr)) ? 1 : 0;
                }
                break;
            case '&':
                if (*(*expr+1) == '&') {
                    (*expr) += 2;
                    left = (left && uss_eval_primary(expr)) ? 1 : 0;
                } else {
                    return left;
                }
                break;
            case '|':
                if (*(*expr+1) == '|') {
                    (*expr) += 2;
                    left = (left || uss_eval_primary(expr)) ? 1 : 0;
                } else {
                    return left;
                }
                break;
            default:
                return left;
        }
    }
}

// ==================== КОМАНДЫ USS ====================

static void uss_cmd_printf(char* args) {
    skip_spaces(&args);
    if (*args != '(') return;
    args++;
    
    while (*args && *args != ')') {
        skip_spaces(&args);
        
        if (*args == '"') {
            char* str = parse_string(&args);
            uss_print(str);
        } else {
            char* ident = parse_ident(&args);
            if (ident) {
                char* str_val = uss_get_var_str(ident);
                if (str_val) {
                    uss_print(str_val);
                } else {
                    u32 num_val = uss_get_var_num(ident);
                    uss_print_num(num_val);
                }
            }
        }
        
        skip_spaces(&args);
        if (*args == ',') args++;
    }
    uss_print("\n");
}

static void uss_cmd_input(char* args) {
    skip_spaces(&args);
    if (*args != '(') return;
    args++;
    
    char* prompt = NULL;
    char* var = NULL;
    
    skip_spaces(&args);
    if (*args == '"') {
        prompt = parse_string(&args);
        uss_print(prompt);
    }
    
    skip_spaces(&args);
    if (*args == ',') args++;
    skip_spaces(&args);
    
    var = parse_ident(&args);
    
    char buf[USS_MAX_LINE];
    int pos = 0;
    
    while (1) {
        if (keyboard_data_ready()) {
            char c = keyboard_getc();
            
            if (c == '\n') {
                buf[pos] = '\0';
                uss_print("\n");
                break;
            } else if (c == '\b') {
                if (pos > 0) {
                    pos--;
                    uss_print("\b \b");
                }
            } else {
                buf[pos++] = c;
                char str[2] = {c, '\0'};
                uss_print(str);
            }
        }
    }
    
    int is_number = 1;
    for (int i = 0; i < pos; i++) {
        if (buf[i] < '0' || buf[i] > '9') {
            is_number = 0;
            break;
        }
    }
    
    if (var) {
        if (is_number && pos > 0) {
            u32 val = 0;
            for (int i = 0; i < pos; i++) {
                val = val * 10 + (buf[i] - '0');
            }
            uss_set_var_num(var, val);
        } else {
            uss_set_var_str(var, buf);
        }
    }
}

static void uss_cmd_assign(char* line) {
    char var[64];
    int i = 0;
    
    skip_spaces(&line);
    
    while (*line && *line != '=' && i < 63) {
        var[i++] = *line++;
    }
    var[i] = '\0';
    while (i > 0 && var[i-1] == ' ') var[--i] = '\0';
    
    if (*line != '=') return;
    line++;
    
    skip_spaces(&line);
    
    if (*line == '"') {
        char* str_val = parse_string(&line);
        uss_set_var_str(var, str_val);
    } else {
        u32 val = uss_eval_expression(&line);
        uss_set_var_num(var, val);
    }
}

static void uss_cmd_if(char* line, char** next_line) {
    skip_spaces(&line);
    if (*line != '(') return;
    line++;
    
    u32 cond = uss_eval_expression(&line);
    
    skip_spaces(&line);
    if (*line == ')') line++;
    
    skip_spaces(&line);
    if (*line != '{') return;
    line++;
    
    if (cond) {
        while (*line && *line != '}') {
            char block_line[USS_MAX_LINE];
            int i = 0;
            while (*line && *line != '\n' && i < USS_MAX_LINE-1) {
                block_line[i++] = *line++;
            }
            block_line[i] = '\0';
            if (*line == '\n') line++;
            
            uss_execute_line(block_line);
            if (!uss.running) break;
        }
        if (*line == '}') line++;
        
        skip_spaces(&line);
        if (strncmp(line, "else", 4) == 0) {
            line += 4;
            skip_spaces(&line);
            if (*line == '{') {
                int brace_count = 1;
                line++;
                while (*line && brace_count > 0) {
                    if (*line == '{') brace_count++;
                    if (*line == '}') brace_count--;
                    line++;
                }
            }
        }
    } else {
        int brace_count = 1;
        while (*line && brace_count > 0) {
            if (*line == '{') brace_count++;
            if (*line == '}') brace_count--;
            line++;
        }
        
        skip_spaces(&line);
        if (strncmp(line, "else", 4) == 0) {
            line += 4;
            skip_spaces(&line);
            if (*line == '{') {
                line++;
                while (*line && *line != '}') {
                    char block_line[USS_MAX_LINE];
                    int i = 0;
                    while (*line && *line != '\n' && i < USS_MAX_LINE-1) {
                        block_line[i++] = *line++;
                    }
                    block_line[i] = '\0';
                    if (*line == '\n') line++;
                    
                    uss_execute_line(block_line);
                    if (!uss.running) break;
                }
                if (*line == '}') line++;
            }
        }
    }
    
    if (next_line) *next_line = line;
}

static void uss_cmd_while(char* line, char** next_line) {
    char* start = line;
    
    skip_spaces(&line);
    if (*line != '(') return;
    line++;
    
    char* cond_start = line;
    int paren_count = 1;
    
    while (*line && paren_count > 0) {
        if (*line == '(') paren_count++;
        if (*line == ')') paren_count--;
        line++;
    }
    
    char* cond_end = line - 1;
    skip_spaces(&line);
    if (*line != '{') return;
    line++;
    
    char* body_start = line;
    int brace_count = 1;
    while (*line && brace_count > 0) {
        if (*line == '{') brace_count++;
        if (*line == '}') brace_count--;
        line++;
    }
    char* body_end = line - 1;
    
    while (uss.running) {
        char* cond_p = cond_start;
        u32 cond_val = uss_eval_expression(&cond_p);
        
        if (!cond_val) break;
        
        char* body_p = body_start;
        while (body_p < body_end && uss.running) {
            char block_line[USS_MAX_LINE];
            int i = 0;
            while (body_p < body_end && *body_p != '\n' && i < USS_MAX_LINE-1) {
                block_line[i++] = *body_p++;
            }
            block_line[i] = '\0';
            if (body_p < body_end && *body_p == '\n') body_p++;
            
            uss_execute_line(block_line);
        }
    }
    
    if (next_line) *next_line = line;
}

static void uss_cmd_return(char* line) {
    skip_spaces(&line);
    
    if (*line) {
        u32 val = uss_eval_expression(&line);
        
        uss_ensure_stack_capacity(uss.return_count + 1);
        
        for (int i = uss.return_count; i > 0; i--) {
            uss.return_stack[i] = uss.return_stack[i-1];
        }
        uss.return_stack[0] = val;
        uss.return_count++;
        
        uss_set_var_num("return", val);
        uss_set_var_num("return.len", uss.return_count);
    }
}

static void uss_cmd_syscall(char* line) {
    skip_spaces(&line);
    if (*line != '(') return;
    line++;
    
    u32 num = uss_eval_expression(&line);
    
    skip_spaces(&line);
    if (*line == ',') line++;
    
    u32 args[6] = {0,0,0,0,0,0};
    for (int i = 0; i < 6; i++) {
        skip_spaces(&line);
        if (*line == ',' || *line == ')') break;
        
        args[i] = uss_eval_expression(&line);
        
        skip_spaces(&line);
        if (*line == ',') line++;
    }
    
    u32 result = syscall(num, args[0], args[1], args[2], args[3], args[4], args[5]);
    uss_set_var_num("result", result);
}

static void uss_cmd_exec(char* line) {
    skip_spaces(&line);
    if (*line != '(') return;
    line++;
    
    skip_spaces(&line);
    char* path = NULL;
    if (*line == '"') {
        path = parse_string(&line);
    }
    
    if (path) {
        long result = syscall(SYS_exec, (long)path, 0, 0, 0, 0, 0);
        uss_set_var_num("result", result);
    }
}

static void uss_cmd_open(char* line) {
    skip_spaces(&line);
    if (*line != '(') return;
    line++;
    
    skip_spaces(&line);
    char* path = NULL;
    if (*line == '"') {
        path = parse_string(&line);
    }
    
    skip_spaces(&line);
    if (*line == ',') line++;
    skip_spaces(&line);
    
    u32 flags = uss_eval_expression(&line);
    
    if (path) {
        long fd = syscall(SYS_open, (long)path, flags, 0, 0, 0, 0);
        uss_set_var_num("fd", fd);
    }
}

static void uss_cmd_read(char* line) {
    skip_spaces(&line);
    if (*line != '(') return;
    line++;
    
    u32 fd = uss_eval_expression(&line);
    
    skip_spaces(&line);
    if (*line == ',') line++;
    skip_spaces(&line);
    
    u32 size = uss_eval_expression(&line);
    
    char* buf = kmalloc(size + 1);
    if (buf) {
        long bytes = syscall(SYS_read, fd, (long)buf, size, 0, 0, 0);
        if (bytes > 0) {
            buf[bytes] = '\0';
            uss_set_var_str("buffer", buf);
        }
        uss_set_var_num("bytes", bytes);
        kfree(buf);
    }
}

static void uss_cmd_write(char* line) {
    skip_spaces(&line);
    if (*line != '(') return;
    line++;
    
    u32 fd = uss_eval_expression(&line);
    
    skip_spaces(&line);
    if (*line == ',') line++;
    skip_spaces(&line);
    
    char* data = NULL;
    if (*line == '"') {
        data = parse_string(&line);
    } else {
        char* var = parse_ident(&line);
        if (var) {
            data = uss_get_var_str(var);
        }
    }
    
    if (data) {
        long bytes = syscall(SYS_write, fd, (long)data, strlen(data), 0, 0, 0);
        uss_set_var_num("bytes", bytes);
    }
}

static void uss_cmd_close(char* line) {
    skip_spaces(&line);
    if (*line != '(') return;
    line++;
    
    u32 fd = uss_eval_expression(&line);
    syscall(SYS_close, fd, 0, 0, 0, 0, 0);
}

static void uss_cmd_disks(char* line) {
    (void)line;
    
    disk_info_t disks[4];
    int count = 0;
    
    udisk_scan();
    
    for (int i = 0; i < 4; i++) {
        disk_info_t* d = udisk_get_info(i);
        if (d && d->present) {
            memcpy(&disks[count], d, sizeof(disk_info_t));
            count++;
        }
    }
    
    for (int i = 0; i < count; i++) {
        char name[16] = "/dev/sdX";
        name[7] = 'a' + disks[i].disk_num;
        
        uss_print(name);
        uss_print("  ");
        uss_print_num((u32)(disks[i].total_sectors * 512 / (1024*1024)));
        uss_print(" MB  ");
        uss_print(disks[i].model);
        uss_print(disks[i].is_gpt ? "  GPT" : "  MBR");
        uss_print("\n");
        
        for (int j = 0; j < disks[i].partition_count; j++) {
            if (!disks[i].partitions[j].present) continue;
            
            char pname[16] = "/dev/sdX";
            pname[7] = 'a' + disks[i].disk_num;
            
            int part_num = disks[i].partitions[j].partition_num;
            if (part_num < 10) {
                pname[8] = '0' + part_num;
                pname[9] = '\0';
            } else {
                pname[8] = '0' + part_num / 10;
                pname[9] = '0' + part_num % 10;
                pname[10] = '\0';
            }
            
            uss_print("  ");
            uss_print(pname);
            uss_print("  ");
            uss_print_num((u32)(disks[i].partitions[j].size / (1024*1024)));
            uss_print(" MB  ");
            
            switch(disks[i].partitions[j].type) {
                case PARTITION_UFS: uss_print("UFS"); break;
                case PARTITION_FAT32: uss_print("FAT32"); break;
                case PARTITION_EXT4: uss_print("EXT4"); break;
                default: uss_print("unknown"); break;
            }
            uss_print("\n");
        }
    }
}

static void uss_cmd_mkfs(char* line) {
    skip_spaces(&line);
    if (*line != '(') return;
    line++;
    
    skip_spaces(&line);
    char* dev = NULL;
    if (*line == '"') {
        dev = parse_string(&line);
    }
    
    if (dev) {
        long res = udisk_format_partition(dev, "ufs");
        uss_set_var_num("result", res);
    }
}

static void uss_cmd_mount(char* line) {
    skip_spaces(&line);
    if (*line != '(') return;
    line++;
    
    skip_spaces(&line);
    char* dev = NULL;
    if (*line == '"') {
        dev = parse_string(&line);
    }
    
    skip_spaces(&line);
    if (*line == ',') line++;
    skip_spaces(&line);
    
    char* point = "/";
    if (*line == '"') {
        point = parse_string(&line);
    }
    
    if (dev) {
        partition_t* p = udisk_get_partition(dev);
        if (p) {
            long res = ufs_mount_with_point(p->start_lba, p->disk_num, point);
            uss_set_var_num("result", res);
            if (res == 0) {
                extern void fs_set_current_dir(const char*);
                fs_set_current_dir(point);
            }
        } else {
            uss_set_var_num("result", -1);
        }
    }
}

static void uss_cmd_umount(char* line) {
    (void)line;
    long res = ufs_umount();
    uss_set_var_num("result", res);
}

static void uss_cmd_ls(char* line) {
    skip_spaces(&line);
    char* path = "/";
    if (*line == '"') {
        path = parse_string(&line);
    }
    
    FSNode* entries;
    u32 count;
    
    if (ufs_readdir(path, &entries, &count) == 0) {
        for (u32 i = 0; i < count; i++) {
            uss_print(entries[i].is_dir ? "d " : "- ");
            uss_print(entries[i].name);
            int len = strlen(entries[i].name);
            for (int j = len; j < 20; j++) uss_print(" ");
            uss_print_num(entries[i].size);
            uss_print(" B\n");
        }
        kfree(entries);
    }
}

static void uss_cmd_cat(char* line) {
    skip_spaces(&line);
    if (*line != '(') return;
    line++;
    
    skip_spaces(&line);
    char* path = NULL;
    if (*line == '"') {
        path = parse_string(&line);
    }
    
    if (path) {
        u8* data;
        u32 size;
        if (ufs_read(path, &data, &size) == 0) {
            uss_print((char*)data);
            if (data[size-1] != '\n') uss_print("\n");
            kfree(data);
        }
    }
}

static void uss_cmd_cd(char* line) {
    skip_spaces(&line);
    char* path = "/";
    if (*line == '"') {
        path = parse_string(&line);
    }
    
    if (ufs_isdir(path)) {
        extern void fs_set_current_dir(const char*);
        fs_set_current_dir(path);
        uss_set_var_num("result", 0);
    } else {
        uss_set_var_num("result", -1);
    }
}

static void uss_cmd_pwd(char* line) {
    (void)line;
    extern const char* fs_get_current_dir(void);
    const char* cwd = fs_get_current_dir();
    uss_print(cwd);
    uss_print("\n");
}

static void uss_cmd_mkdir(char* line) {
    skip_spaces(&line);
    if (*line != '(') return;
    line++;
    
    skip_spaces(&line);
    char* path = NULL;
    if (*line == '"') {
        path = parse_string(&line);
    }
    
    if (path) {
        long res = ufs_mkdir(path);
        uss_set_var_num("result", res);
    }
}

static void uss_cmd_rm(char* line) {
    skip_spaces(&line);
    if (*line != '(') return;
    line++;
    
    skip_spaces(&line);
    char* path = NULL;
    if (*line == '"') {
        path = parse_string(&line);
    }
    
    if (path) {
        long res;
        if (ufs_isdir(path)) {
            res = ufs_rmdir_force(path);
        } else {
            res = ufs_delete(path);
        }
        uss_set_var_num("result", res);
    }
}

// ==================== ОСНОВНОЙ ИНТЕРПРЕТАТОР ====================

void uss_init(void) {
    uss.vars = NULL;
    uss.var_count = 0;
    uss.var_capacity = 0;
    
    uss.return_stack = NULL;
    uss.return_count = 0;
    uss.return_capacity = 0;
    
    uss.script_data = NULL;
    uss.line_num = 0;
    uss.position = 0;
    uss.running = 1;
    
    uss_ensure_var_capacity(USS_INIT_VARS);
    uss_ensure_stack_capacity(USS_INIT_STACK);
    
    uss_set_var_num("true", 1);
    uss_set_var_num("false", 0);
    uss_set_var_num("result", 0);
    uss_set_var_num("return", 0);
    uss_set_var_num("return.len", 0);
    uss_set_var_num("fd", -1);
    uss_set_var_num("bytes", 0);
}

static void uss_execute_line(char* line) {
    skip_spaces(&line);
    
    if (*line == '\0' || *line == '\n') return;
    if (*line == '/' && *(line+1) == '/') return;
    
    if (strncmp(line, "printf", 6) == 0) {
        uss_cmd_printf(line + 6);
    }
    else if (strncmp(line, "input", 5) == 0) {
        uss_cmd_input(line + 5);
    }
    else if (strncmp(line, "if", 2) == 0) {
        uss_cmd_if(line + 2, NULL);
    }
    else if (strncmp(line, "while", 5) == 0) {
        uss_cmd_while(line + 5, NULL);
    }
    else if (strncmp(line, "return", 6) == 0) {
        uss_cmd_return(line + 6);
    }
    else if (strncmp(line, "syscall", 7) == 0) {
        uss_cmd_syscall(line + 7);
    }
    else if (strncmp(line, "exec", 4) == 0) {
        uss_cmd_exec(line + 4);
    }
    else if (strncmp(line, "open", 4) == 0) {
        uss_cmd_open(line + 4);
    }
    else if (strncmp(line, "read", 4) == 0) {
        uss_cmd_read(line + 4);
    }
    else if (strncmp(line, "write", 5) == 0) {
        uss_cmd_write(line + 5);
    }
    else if (strncmp(line, "close", 5) == 0) {
        uss_cmd_close(line + 5);
    }
    else if (strncmp(line, "disks", 5) == 0) {
        uss_cmd_disks(line + 5);
    }
    else if (strncmp(line, "mkfs", 4) == 0) {
        uss_cmd_mkfs(line + 4);
    }
    else if (strncmp(line, "mount", 5) == 0) {
        uss_cmd_mount(line + 5);
    }
    else if (strncmp(line, "umount", 6) == 0) {
        uss_cmd_umount(line + 6);
    }
    else if (strncmp(line, "ls", 2) == 0) {
        uss_cmd_ls(line + 2);
    }
    else if (strncmp(line, "cat", 3) == 0) {
        uss_cmd_cat(line + 3);
    }
    else if (strncmp(line, "cd", 2) == 0) {
        uss_cmd_cd(line + 2);
    }
    else if (strncmp(line, "pwd", 3) == 0) {
        uss_cmd_pwd(line + 3);
    }
    else if (strncmp(line, "mkdir", 5) == 0) {
        uss_cmd_mkdir(line + 5);
    }
    else if (strncmp(line, "rm", 2) == 0) {
        uss_cmd_rm(line + 2);
    }
    else if (strchr(line, '=')) {
        uss_cmd_assign(line);
    }
}

int uss_execute(const char* path) {
    uss_init();
    
    u8 *script;
    u32 size;
    
    if (ufs_read(path, &script, &size) != 0) {
        uss_print("USS: script not found - ");
        uss_print(path);
        uss_print("\n");
        return -1;
    }
    
    uss_print("USS: executing ");
    uss_print(path);
    uss_print("\n");
    
    uss.script_data = (char*)script;
    uss.line_num = 1;
    
    char* p = (char*)script;
    char line[USS_MAX_LINE];
    
    while (*p && uss.running) {
        int i = 0;
        while (*p && *p != '\n' && i < USS_MAX_LINE-1) {
            line[i++] = *p++;
        }
        line[i] = '\0';
        if (*p == '\n') p++;
        
        uss_execute_line(line);
        uss.line_num++;
    }
    
    kfree(script);
    
    for (int i = 0; i < uss.var_count; i++) {
        if (uss.vars[i].type == VAL_STRING && uss.vars[i].data.str_val) {
            kfree(uss.vars[i].data.str_val);
        }
    }
    
    if (uss.vars) kfree(uss.vars);
    if (uss.return_stack) kfree(uss.return_stack);
    
    return 0;
}
