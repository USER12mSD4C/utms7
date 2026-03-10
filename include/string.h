#ifndef STRING_H
#define STRING_H

#include "types.h"

void* memcpy(void* dest, const void* src, u64 n);
void* memset(void* s, int c, u64 n);
int memcmp(const void* s1, const void* s2, u64 n);
char* strcpy(char* dest, const char* src);
char* strncpy(char* dest, const char* src, u64 n);
int strcmp(const char* s1, const char* s2);
int strncmp(const char* s1, const char* s2, u64 n);
u64 strlen(const char* s);
char* strstr(const char* haystack, const char* needle);
char* strchr(const char* s, int c);
char* strrchr(const char* s, int c);
char* strcat(char* dest, const char* src);           // ДОБАВЛЕНО
char* strtok(char* str, const char* delim);          // ДОБАВЛЕНО
int sprintf(char* str, const char* format, ...);
int snprintf(char* str, u64 size, const char* format, ...);

#endif
