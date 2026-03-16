#ifndef ZLIB_H
#define ZLIB_H

#include "../include/types.h"

int zlib_inflate(u8 *in, u32 in_len, u8 **out, u32 *out_len);

#endif
