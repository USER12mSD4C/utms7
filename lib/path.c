#include "../include/string.h"
#include "../commands/fs.h"
#include "../include/path.h"

// Нормализация пути (убирает . и ..)
static void normalize_path(char* path) {
    char temp[256];
    strcpy(temp, path);
    
    char *parts[64];
    int part_count = 0;
    
    char *p = strtok(temp, "/");
    while (p && part_count < 64) {
        if (strcmp(p, "..") == 0) {
            if (part_count > 0) part_count--;
        } else if (strcmp(p, ".") != 0 && p[0] != '\0') {
            parts[part_count++] = p;
        }
        p = strtok(NULL, "/");
    }
    
    if (part_count == 0) {
        strcpy(path, "/");
    } else {
        path[0] = '\0';
        for (int i = 0; i < part_count; i++) {
            strcat(path, "/");
            strcat(path, parts[i]);
        }
    }
}

// Публичная функция для построения пути
void build_path(const char* arg, char* result) {
    if (!arg || arg[0] == '\0') {
        strcpy(result, "/");
        return;
    }
    
    const char *cwd = fs_get_current_dir();
    if (!cwd) cwd = "/";
    
    char temp[512];
    
    if (arg[0] == '/') {
        strcpy(temp, arg);
    }
    else if (strcmp(cwd, "/") == 0) {
        temp[0] = '/';
        temp[1] = '\0';
        strcat(temp, arg);
    }
    else {
        strcpy(temp, cwd);
        strcat(temp, "/");
        strcat(temp, arg);
    }
    
    normalize_path(temp);
    strcpy(result, temp);
}
