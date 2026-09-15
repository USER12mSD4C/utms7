#ifndef _UAPI_LINUX_TYPES_H
#define _UAPI_LINUX_TYPES_H

#include "../../types.h"

#define __BITS_PER_LONG 64

typedef __u16 __bitwise __le16;
typedef __u32 __bitwise __le32;
typedef __u64 __bitwise __le64;
typedef __u16 __bitwise __be16;
typedef __u32 __bitwise __be32;
typedef __u64 __bitwise __be64;
typedef __u64 __aligned_u64;
typedef __s64 __aligned_s64;

#endif
