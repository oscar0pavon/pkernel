
#ifndef __TYPES_H__
#define __TYPES_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef uint64_t EfiPhysicalAddress;

typedef uint8_t byte;

#define ELFABI __attribute__((sysv_abi))

#endif
