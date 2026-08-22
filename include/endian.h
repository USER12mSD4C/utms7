// include/endian.h
#ifndef ENDIAN_H
#define ENDIAN_H

#include "types.h"

static inline u16 bswap16(u16 x) {
    return (x >> 8) | (x << 8);
}

static inline u32 bswap32(u32 x) {
    return (x >> 24) | ((x >> 8) & 0xFF00) | ((x << 8) & 0xFF0000) | (x << 24);
}

static inline u64 bswap64(u64 x) {
    return (x >> 56) | ((x >> 40) & 0xFF00) | ((x >> 24) & 0xFF0000) |
           ((x >> 8) & 0xFF000000) | ((x << 8) & 0xFF00000000ULL) |
           ((x << 24) & 0xFF0000000000ULL) | ((x << 40) & 0xFF000000000000ULL) |
           (x << 56);
}

static inline u16 htons(u16 x) { return bswap16(x); }
static inline u32 htonl(u32 x) { return bswap32(x); }
static inline u64 htonll(u64 x) { return bswap64(x); }

static inline u16 ntohs(u16 x) { return bswap16(x); }
static inline u32 ntohl(u32 x) { return bswap32(x); }
static inline u64 ntohll(u64 x) { return bswap64(x); }

#endif
