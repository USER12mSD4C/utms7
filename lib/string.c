#include "libc.h"
#include <stdint.h>

void *memcpy(void *dest, const void *src, size_t n) {
    void *ret = dest;
    __asm__ volatile(
        "rep movsb"
        : "+D"(dest), "+S"(src), "+c"(n)
        : : "memory"
    );
    return ret;
}

void *memset(void *s, int c, size_t n) {
    void *ret = s;
    __asm__ volatile(
        "rep stosb"
        : "+D"(s), "+c"(n)
        : "a"((unsigned char)c)
        : "memory"
    );
    return ret;
}

void *memmove(void *dest, const void *src, size_t n) {
    if (dest < src) return memcpy(dest, src, n);
    if (dest > src) {
        uint8_t *d = (uint8_t*)dest + n;
        const uint8_t *s = (const uint8_t*)src + n;
        __asm__ volatile(
            "std\n\t"
            "rep movsb\n\t"
            "cld"
            : "+D"(d), "+S"(s), "+c"(n)
            : : "memory"
        );
    }
    return dest;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const uint8_t *p1 = (const uint8_t*)s1, *p2 = (const uint8_t*)s2;
    while (n--) { if (*p1 != *p2) return *p1 - *p2; p1++; p2++; }
    return 0;
}

void *memchr(const void *s, int c, size_t n) {
    const uint8_t *p = (const uint8_t*)s;
    while (n--) { if (*p == (uint8_t)c) return (void*)p; p++; }
    return NULL;
}

size_t strlen(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

char *strcpy(char *dest, const char *src) {
    char *ret = dest;
    while ((*dest++ = *src++));
    return ret;
}

char *strncpy(char *dest, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i]; i++) dest[i] = src[i];
    for (; i < n; i++) dest[i] = '\0';
    return dest;
}

char *strcat(char *dest, const char *src) {
    char *ret = dest;
    while (*dest) dest++;
    while ((*dest++ = *src++));
    return ret;
}

char *strncat(char *dest, const char *src, size_t n) {
    char *ret = dest;
    while (*dest) dest++;
    while (n-- && *src) *dest++ = *src++;
    *dest = '\0';
    return ret;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 && *s1 == *s2) { s1++; s2++; }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    while (n && *s1 && *s1 == *s2) { s1++; s2++; n--; }
    if (n == 0) return 0;
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

char *strchr(const char *s, int c) {
    while (*s) { if (*s == (char)c) return (char*)s; s++; }
    return (c == '\0') ? (char*)s : NULL;
}

char *strrchr(const char *s, int c) {
    const char *found = NULL;
    while (*s) { if (*s == (char)c) found = s; s++; }
    if (c == '\0') return (char*)s;
    return (char*)found;
}

char *strstr(const char *haystack, const char *needle) {
    if (!*needle) return (char*)haystack;
    for (; *haystack; haystack++) {
        const char *h = haystack, *n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return (char*)haystack;
    }
    return NULL;
}

char *strdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *new = malloc(len);
    if (new) memcpy(new, s, len);
    return new;
}

char *strtok(char *str, const char *delim) {
    static char *saved;
    if (str) saved = str;
    if (!saved) return NULL;
    while (*saved && strchr(delim, *saved)) saved++;
    if (!*saved) return NULL;
    char *token = saved;
    while (*saved && !strchr(delim, *saved)) saved++;
    if (*saved) { *saved = '\0'; saved++; } else { saved = NULL; }
    return token;
}
