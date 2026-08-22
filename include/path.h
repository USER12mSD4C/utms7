#ifndef PATH_H
#define PATH_H

#include "types.h"

// Построение абсолютного пути из относительного
// arg - относительный или абсолютный путь
// result - буфер для результата (минимум 256 байт)
void build_path(const char* arg, char* result);

#endif
