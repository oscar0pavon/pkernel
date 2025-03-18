
#ifndef __TYPES_H__
#define __TYPES_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef uint64_t EfiPhysicalAddress;

typedef uint8_t byte;
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#define ELFABI __attribute__((sysv_abi))
#define SYSVABI __attribute__((sysv_abi))

#endif
