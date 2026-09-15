#ifndef LINUX_COMPAT_TYPES_H
#define LINUX_COMPAT_TYPES_H

#include "../include/types.h"

typedef u8  __u8;
typedef u16 __u16;
typedef u32 __u32;
typedef u64 __u64;
typedef i8  __s8;
typedef i16 __s16;
typedef i32 __s32;
typedef i64 __s64;

typedef u8  u8;
typedef u16 u16;
typedef u32 u32;
typedef u64 u64;
typedef i8  s8;
typedef i16 s16;
typedef i32 s32;
typedef i64 s64;

typedef unsigned int uint;
typedef unsigned long ulong;
#if !defined(_SIZE_T) && !defined(_SIZE_T_DEFINED)
typedef unsigned long size_t;
#define _SIZE_T
#endif
typedef long ssize_t;

#define bool int
#define true 1
#define false 0

#define __iomem
#define __user
#define __packed __attribute__((packed))
#define __aligned(x) __attribute__((aligned(x)))
#define __bitwise
#define __force
#define asmlinkage

#define BIT(x) (1UL << (x))
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#define min(a,b) ((a) < (b) ? (a) : (b))
#define max(a,b) ((a) > (b) ? (a) : (b))

#define container_of(ptr, type, member) ({                      \
    const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
    (type *)( (char *)__mptr - offsetof(type,member) );})

#ifndef offsetof
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#endif

#define EXPORT_SYMBOL(x)
#define EXPORT_SYMBOL_GPL(x)
#define MODULE_LICENSE(x)
#define MODULE_AUTHOR(x)
#define MODULE_DESCRIPTION(x)
#define MODULE_DEVICE_TABLE(name, table)

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#endif
