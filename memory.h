#ifndef MEMORY_H
#define MEMORY_H

#include "types.h"
#include <stddef.h>

#define aligned4k __attribute__((aligned(4096)))
#define aligned64 __attribute__((aligned(64)))

#define XDBG(...) do { if (xhci_debug) printf(__VA_ARGS__); } while(0)

struct MemoryDescriptor {
  uint32_t type;
  uint32_t padding;
  uint64_t physical_start;
  uint64_t virtual_start;
  uint64_t pages;
  uint64_t attributes;
} __attribute__((packed));

typedef struct MemoryMapInfo{
  uint64_t buffer_address; // Absolute physical memory address of the mmap data (0x5000000)
  uint64_t total_size;     // Total bytes populated in the buffer
  uint64_t descriptor_size;// Unique hardware stepping size (usually 40 or 48 bytes)
}MemoryMapInfo;

// Physical page allocator (4 KB pages, identity-mapped)
void  pmm_init(void *boot_info);
void *pmm_alloc_page(void);
void  pmm_free_page(void *page);

// Heap allocator
void *kmalloc(size_t size);
void  kfree(void *ptr);

#endif
