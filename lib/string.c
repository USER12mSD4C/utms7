#include "../include/string.h"

void* memcpy(void* dest, const void* src, u64 n) {
    u8* d = (u8*)dest;
    const u8* s = (const u8*)src;
    
    // Копируем по байтам, без SSE оптимизаций
    for (u64 i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dest;
}

void* memset(void* s, int c, u64 n) {
    u8* p = (u8*)s;
    u8 val = (u8)c;
    
    // Заполняем по байтам, без SSE оптимизаций
    for (u64 i = 0; i < n; i++) {
        p[i] = val;
    }
    return s;
}

int memcmp(const void* s1, const void* s2, u64 n) {
    const u8* p1 = (const u8*)s1;
    const u8* p2 = (const u8*)s2;
    for (u64 i = 0; i < n; i++) {
        if (p1[i] != p2[i]) return p1[i] - p2[i];
    }
    return 0;
}

char* strcpy(char* dest, const char* src) {
    char* d = dest;
    while (*src) {
        *d++ = *src++;
    }
    *d = '\0';
    return dest;
}

char* strncpy(char* dest, const char* src, u64 n) {
    char* d = dest;
    u64 i;
    for (i = 0; i < n && src[i]; i++) {
        d[i] = src[i];
    }
    for (; i < n; i++) {
        d[i] = '\0';
    }
    return dest;
}

int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const u8*)s1 - *(const u8*)s2;
}

int strncmp(const char* s1, const char* s2, u64 n) {
    for (u64 i = 0; i < n; i++) {
        if (s1[i] != s2[i]) return s1[i] - s2[i];
        if (s1[i] == '\0') break;
    }
    return 0;
}

u64 strlen(const char* s) {
    u64 len = 0;
    while (s[len]) len++;
    return len;
}

char* strchr(const char* s, int c) {
    while (*s) {
        if (*s == (char)c) return (char*)s;
        s++;
    }
    return (c == '\0') ? (char*)s : NULL;
}

char* strrchr(const char* s, int c) {
    const char* last = NULL;
    while (*s) {
        if (*s == (char)c) last = s;
        s++;
    }
    if (c == '\0') return (char*)s;
    return (char*)last;
}

char* strstr(const char* haystack, const char* needle) {
    if (!*needle) return (char*)haystack;
    
    while (*haystack) {
        const char* h = haystack;
        const char* n = needle;
        
        while (*h && *n && (*h == *n)) {
            h++;
            n++;
        }
        
        if (!*n) return (char*)haystack;
        haystack++;
    }
    
    return NULL;
}

char* strcat(char* dest, const char* src) {
    char* d = dest;
    while (*d) d++;
    while (*src) {
        *d++ = *src++;
    }
    *d = '\0';
    return dest;
}

static char* strtok_pos = NULL;

char* strtok(char* str, const char* delim) {
    if (str) {
        strtok_pos = str;
    }
    
    if (!strtok_pos || *strtok_pos == '\0') {
        return NULL;
    }
    
    // Пропускаем ведущие разделители
    while (*strtok_pos) {
        const char* d = delim;
        int is_delim = 0;
        while (*d) {
            if (*strtok_pos == *d) {
                is_delim = 1;
                break;
            }
            d++;
        }
        if (!is_delim) break;
        strtok_pos++;
    }
    
    char* start = strtok_pos;
    
    if (*strtok_pos == '\0') {
        strtok_pos = NULL;
        return start;
    }
    
    // Ищем следующий разделитель
    while (*strtok_pos) {
        const char* d = delim;
        int is_delim = 0;
        while (*d) {
            if (*strtok_pos == *d) {
                is_delim = 1;
                break;
            }
            d++;
        }
        if (is_delim) {
            *strtok_pos = '\0';
            strtok_pos++;
            return start;
        }
        strtok_pos++;
    }
    
    strtok_pos = NULL;
    return start;
}

static void reverse(char* str, int len) {
    int i = 0, j = len - 1;
    while (i < j) {
        char tmp = str[i];
        str[i] = str[j];
        str[j] = tmp;
        i++;
        j--;
    }
}

static int utoa(u32 num, char* str, int base) {
    int i = 0;
    
    if (num == 0) {
        str[i++] = '0';
        str[i] = '\0';
        return i;
    }
    
    while (num != 0) {
        u32 rem = num % base;
        str[i++] = (rem > 9) ? (rem - 10) + 'a' : rem + '0';
        num = num / base;
    }
    
    str[i] = '\0';
    reverse(str, i);
    return i;
}

static int itoa(i32 num, char* str, int base) {
    if (base == 10 && num < 0) {
        str[0] = '-';
        int len = utoa(-num, str + 1, base);
        return len + 1;
    }
    return utoa(num, str, base);
}

int sprintf(char* str, const char* format, ...) {
    char* buf = str;
    const char* f = format;
    u64* arg = (u64*)(&format + 1);
    
    while (*f) {
        if (*f == '%') {
            f++;
            switch (*f) {
                case 'd': {
                    char num[32];
                    int len = itoa((i32)(*arg++), num, 10);
                    for (int i = 0; i < len; i++) *buf++ = num[i];
                    break;
                }
                case 'u': {
                    char num[32];
                    int len = utoa((u32)(*arg++), num, 10);
                    for (int i = 0; i < len; i++) *buf++ = num[i];
                    break;
                }
                case 'x': {
                    char num[32];
                    int len = utoa((u32)(*arg++), num, 16);
                    for (int i = 0; i < len; i++) *buf++ = num[i];
                    break;
                }
                case 's': {
                    char* s = (char*)(*arg++);
                    while (*s) *buf++ = *s++;
                    break;
                }
                case 'c': {
                    *buf++ = (char)(*arg++);
                    break;
                }
                default:
                    *buf++ = '%';
                    *buf++ = *f;
                    break;
            }
            f++;
        } else {
            *buf++ = *f++;
        }
    }
    *buf = '\0';
    return buf - str;
}

int snprintf(char* str, u64 size, const char* format, ...) {
    char buf[1024];
    u64* arg = (u64*)(&format + 1);
    int len = sprintf(buf, format, *arg, *(arg+1), *(arg+2), *(arg+3), *(arg+4), *(arg+5));
    
    u64 copy_len = (len < (int)size - 1) ? len : size - 1;
    for (u64 i = 0; i < copy_len; i++) {
        str[i] = buf[i];
    }
    str[copy_len] = '\0';
    return len;
}
