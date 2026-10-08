/* gctypes.h for the host: the fixed-width names libogc gives these types, and
 * nothing else. The sources compiled by tests/host/ include it only for these. */
#ifndef HOST_SHIM_GCTYPES_H
#define HOST_SHIM_GCTYPES_H

#include <stdbool.h>
#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

#endif
