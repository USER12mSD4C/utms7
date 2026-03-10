#include "../include/string.h"
#include "../drivers/vga.h"
#include "../drivers/keyboard.h"
#include "../fs/ufs.h"
#include "../kernel/kapi.h"
#include "../kernel/memory.h"
#include "uss.h"

#define USS_MAX_VARS 256
#define USS_MAX_STACK 128
#define USS_MAX_LINE 512
#define USS_MAX_ARGS 16

typedef enum {
    VAL_U8, VAL_U16, VAL_U32, VAL_U64,
    VAL_I8, VAL_I16, VAL_I32, VAL_I64,
    VAL_STRING, VAL_PTR, VAL_ARRAY
} ValueType;

typedef struct {
    char name[64];
    ValueType type;
    union {
        u8 u8_val;
        u16 u16_val;
        u32 u32_val;
        u64 u64_val;
        i8 i8_val;
        i16 i16_val;
        i32 i32_val;
        i64 i64_val;
        char* str_val;
        void* ptr_val;
        struct {
            void* data;
            u32 len;
        } array;
    } data;
} USSVar;

typedef struct {
    USSVar vars[USS_MAX_VARS];
    int var_count;
    u32 return_stack[USS_MAX_STACK];
    int return_count;
    char* script_data;
    int line_num;
    int position;
} USS;

static USS uss;

// прототипы
static void uss_execute_line(char* line);
static u32 uss_eval_expr(char** expr);
static int uss_find_var(const char* name);
static void uss_set_var(const char* name, ValueType type, u32 value);
static u32 uss_get_var(const char* name);

void uss_init(void) {
    uss.var_count = 0;
    uss.return_count = 0;
    uss.script_data = NULL;
    uss.line_num = 0;
    uss.position = 0;
    
    // предопределенные переменные
    uss_set_var("return", VAL_U32, 0);
    uss_set_var("return.len", VAL_U32, 0);
    uss_set_var("true", VAL_U32, 1);
    uss_set_var("false", VAL_U32, 0);
}

static int uss_find_var(const char* name) {
    for (int i = 0; i < uss.var_count; i++) {
        if (strcmp(uss.vars[i].name, name) == 0) return i;
    }
    return -1;
}

void uss_set_var(const char* name, ValueType type, u32 value) {
    int idx = uss_find_var(name);
    if (idx == -1) {
        if (uss.var_count >= USS_MAX_VARS) return;
        idx = uss.var_count++;
        strcpy(uss.vars[idx].name, name);
    }
    
    uss.vars[idx].type = type;
    uss.vars[idx].data.u32_val = value;  // упрощенно
}

u32 uss_get_var(const char* name) {
    int idx = uss_find_var(name);
    if (idx == -1) return 0;
    return uss.vars[idx].data.u32_val;
}

// пропуск пробелов
static void skip_spaces(char** p) {
    while (**p == ' ' || **p == '\t') (*p)++;
}

// парсинг числа
static u32 parse_number(char** p) {
    u32 val = 0;
    while (**p >= '0' && **p <= '9') {
        val = val * 10 + (**p - '0');
        (*p)++;
    }
    return val;
}

// парсинг идентификатора
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

// парсинг строки в кавычках
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

// ============= КОМАНДЫ =============

// printf("text", var1, var2)
static void uss_cmd_printf(char* args) {
    skip_spaces(&args);
    if (*args != '(') return;
    args++;
    
    while (*args && *args != ')') {
        skip_spaces(&args);
        
        if (*args == '"') {
            char* str = parse_string(&args);
            vga_write(str);
        } else {
            char* ident = parse_ident(&args);
            if (ident) {
                u32 val = uss_get_var(ident);
                // TODO: нормальный вывод чисел
                char buf[16];
                int len = sprintf(buf, "%d", val);
                for (int i = 0; i < len; i++) vga_putchar(buf[i]);
            }
        }
        
        skip_spaces(&args);
        if (*args == ',') args++;
    }
    vga_putchar('\n');
}

// input("prompt", var)
static void uss_cmd_input(char* args) {
    skip_spaces(&args);
    if (*args != '(') return;
    args++;
    
    char* prompt = NULL;
    char* var = NULL;
    
    skip_spaces(&args);
    if (*args == '"') {
        prompt = parse_string(&args);
        vga_write(prompt);
    }
    
    skip_spaces(&args);
    if (*args == ',') args++;
    skip_spaces(&args);
    
    var = parse_ident(&args);
    
    // читаем строку с клавиатуры
    char buf[256];
    int pos = 0;
    
    while (1) {
        if (keyboard_data_ready()) {
            char c = keyboard_getc();
            
            if (c == '\n') {
                buf[pos] = '\0';
                vga_putchar('\n');
                break;
            } else if (c == '\b') {
                if (pos > 0) {
                    pos--;
                    vga_putchar('\b');
                }
            } else {
                buf[pos++] = c;
                vga_putchar(c);
            }
        }
    }
    
    // преобразуем в число и сохраняем
    u32 val = 0;
    for (int i = 0; i < pos; i++) {
        if (buf[i] >= '0' && buf[i] <= '9') {
            val = val * 10 + (buf[i] - '0');
        }
    }
    
    if (var) uss_set_var(var, VAL_U32, val);
}

// присваивание
static void uss_cmd_assign(char* line) {
    char var[64];
    int i = 0;
    
    skip_spaces(&line);
    
    // левая часть
    while (*line && *line != '=' && i < 63) {
        var[i++] = *line++;
    }
    var[i] = '\0';
    while (i > 0 && var[i-1] == ' ') var[--i] = '\0';
    
    if (*line != '=') return;
    line++;
    
    skip_spaces(&line);
    
    // правая часть (выражение)
    u32 val = uss_eval_expr(&line);
    uss_set_var(var, VAL_U32, val);
}

// if (condition) { ... } else { ... }
static void uss_cmd_if(char* line, char** next_line) {
    skip_spaces(&line);
    if (*line != '(') return;
    line++;
    
    u32 cond = uss_eval_expr(&line);
    
    skip_spaces(&line);
    if (*line == ')') line++;
    
    skip_spaces(&line);
    if (*line != '{') return;
    line++;
    
    if (cond) {
        // выполняем then-блок
        while (*line && *line != '}') {
            char block_line[USS_MAX_LINE];
            int i = 0;
            while (*line && *line != '\n' && i < USS_MAX_LINE-1) {
                block_line[i++] = *line++;
            }
            block_line[i] = '\0';
            if (*line == '\n') line++;
            
            uss_execute_line(block_line);
        }
        if (*line == '}') line++;
        
        // пропускаем else если есть
        skip_spaces(&line);
        if (strncmp(line, "else", 4) == 0) {
            line += 4;
            skip_spaces(&line);
            if (*line == '{') {
                // пропускаем else-блок
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
        // пропускаем then-блок
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
                // выполняем else-блок
                while (*line && *line != '}') {
                    char block_line[USS_MAX_LINE];
                    int i = 0;
                    while (*line && *line != '\n' && i < USS_MAX_LINE-1) {
                        block_line[i++] = *line++;
                    }
                    block_line[i] = '\0';
                    if (*line == '\n') line++;
                    
                    uss_execute_line(block_line);
                }
                if (*line == '}') line++;
            }
        }
    }
    
    if (next_line) *next_line = line;
}

// while (condition) { ... }
static void uss_cmd_while(char* line, char** next_line) {
    char* start = line;
    
    skip_spaces(&line);
    if (*line != '(') return;
    line++;
    
    char* cond_start = line;
    
    // ищем закрывающую скобку
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
    
    // сохраняем тело цикла
    char* body_start = line;
    int brace_count = 1;
    while (*line && brace_count > 0) {
        if (*line == '{') brace_count++;
        if (*line == '}') brace_count--;
        line++;
    }
    char* body_end = line - 1;
    
    // выполняем цикл
    while (1) {
        // вычисляем условие
        char* cond_p = cond_start;
        u32 cond_val = uss_eval_expr(&cond_p);
        
        if (!cond_val) break;
        
        // выполняем тело
        char* body_p = body_start;
        while (body_p < body_end) {
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

// return [value]
static void uss_cmd_return(char* line) {
    skip_spaces(&line);
    
    if (*line) {
        u32 val = uss_eval_expr(&line);
        
        // сдвигаем стек возвратов
        for (int i = USS_MAX_STACK - 1; i > 0; i--) {
            uss.return_stack[i] = uss.return_stack[i-1];
        }
        uss.return_stack[0] = val;
        uss.return_count++;
        if (uss.return_count > USS_MAX_STACK) uss.return_count = USS_MAX_STACK;
        
        uss_set_var("return", VAL_U32, val);
        uss_set_var("return.len", VAL_U32, uss.return_count);
    }
}

// ============= ВЫРАЖЕНИЯ =============

static u32 uss_eval_primary(char** expr) {
    skip_spaces(expr);
    
    if (**expr >= '0' && **expr <= '9') {
        return parse_number(expr);
    }
    
    if (**expr == '"') {
        parse_string(expr);  // строки пока игнорируем
        return 0;
    }
    
    if (**expr == '(') {
        (*expr)++;
        u32 val = uss_eval_expr(expr);
        skip_spaces(expr);
        if (**expr == ')') (*expr)++;
        return val;
    }
    
    char* ident = parse_ident(expr);
    if (ident) {
        return uss_get_var(ident);
    }
    
    return 0;
}

static u32 uss_eval_expr(char** expr) {
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

// ============= ОСНОВНОЙ ИНТЕРПРЕТАТОР =============

static void uss_execute_line(char* line) {
    // убираем пробелы в начале
    skip_spaces(&line);
    
    // пустая строка или комментарий
    if (*line == '\0' || *line == '\n') return;
    if (*line == '/' && *(line+1) == '/') return;
    
    // проверяем команды
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
    else if (strchr(line, '=')) {
        uss_cmd_assign(line);
    }
    else {
        // может быть просто выражение?
        char* p = line;
        uss_eval_expr(&p);
    }
}

int uss_execute(const char* path) {
    uss_init();
    
    u8 *script;
    u32 size;
    
    if (ufs_read(path, &script, &size) != 0) {
        vga_write("USS: script not found - ");
        vga_write(path);
        vga_putchar('\n');
        return -1;
    }
    
    vga_write("USS: executing ");
    vga_write(path);
    vga_putchar('\n');
    
    uss.script_data = (char*)script;
    uss.line_num = 1;
    
    char* p = (char*)script;
    char line[USS_MAX_LINE];
    
    while (*p) {
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
    return 0;
}
