
#include "memory.h"
#include "pkernel.h"
#include "library.h"
#include "console.h"

// ============================================================================
// Physical Page Allocator — bitmap, 4 KB pages, up to 512 MB
// ============================================================================

#define PAGE_SIZE     4096ULL
#define TRACKED_PAGES (512ULL * 1024 * 1024 / PAGE_SIZE)  // 131072
#define BITMAP_BYTES  (TRACKED_PAGES / 8)                  // 16 KB

static uint8_t page_bitmap[BITMAP_BYTES];

// Location of the UEFI memory map captured by the bootloader, kept so callers
// can query what firmware reported as usable RAM after pmm_init() has run.
static uint8_t *mmap_base;
static uint64_t mmap_total;
static uint64_t mmap_stride;

static inline void _set(uint64_t i) { page_bitmap[i / 8] |=  (1u << (i % 8)); }
static inline void _clr(uint64_t i) { page_bitmap[i / 8] &= ~(1u << (i % 8)); }
static inline int  _tst(uint64_t i) { return (page_bitmap[i / 8] >> (i % 8)) & 1; }

static void mark_range(uint64_t base, uint64_t bytes, int used) {
    uint64_t first = base / PAGE_SIZE;
    uint64_t last  = (base + bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint64_t p = first; p < last && p < TRACKED_PAGES; p++)
        used ? _set(p) : _clr(p);
}

extern char _kernel_end[];

void pmm_init(void *info) {
    BootInfo *boot_info = (BootInfo *)info;

    memset(page_bitmap, 0xFF, BITMAP_BYTES);  // start: all pages occupied

    uint8_t  *map    = (uint8_t *)boot_info->memory_info.buffer_address;
    uint64_t  total  = boot_info->memory_info.total_size;
    uint64_t  stride = boot_info->memory_info.descriptor_size;
    uint64_t  count  = total / stride;
    uint64_t  free_bytes = 0;

    mmap_base   = map;    // remember the map for later usability queries
    mmap_total  = total;
    mmap_stride = stride;

    for (uint64_t i = 0; i < count; i++) {
        struct MemoryDescriptor *d =
            (struct MemoryDescriptor *)(map + i * stride);
        if (d->type == 7) {  // EfiConventionalMemory
            mark_range(d->physical_start, d->pages * PAGE_SIZE, 0);
            free_bytes += d->pages * PAGE_SIZE;
        }
    }

    // Keep legacy/BIOS area, kernel binary, and UEFI memory map buffer occupied
    mark_range(0,                                    0x100000, 1);
    mark_range(0x4000000, (uint64_t)_kernel_end - 0x4000000, 1);
    mark_range(boot_info->memory_info.buffer_address,  total, 1);

    printf("PMM: %d MB free\n", (uint32_t)(free_bytes / 1024 / 1024));
}

// True if `phys` lies inside a region firmware reported as EfiConventionalMemory
// (type 7) — i.e. real RAM the kernel may write. Used to confirm the SMP
// trampoline page is backed by usable memory before we copy into it, rather
// than trusting a hardcoded low address. Valid only after pmm_init().
int pmm_phys_usable(uint64_t phys) {
    for (uint64_t i = 0; i < mmap_total / mmap_stride; i++) {
        struct MemoryDescriptor *d =
            (struct MemoryDescriptor *)(mmap_base + i * mmap_stride);
        if (d->type != 7) continue;  // EfiConventionalMemory
        uint64_t start = d->physical_start;
        uint64_t end   = start + d->pages * PAGE_SIZE;
        if (phys >= start && phys < end) return 1;
    }
    return 0;
}

void *pmm_alloc_page(void) {
    for (uint64_t i = 256; i < TRACKED_PAGES; i++) {  // skip first 1 MB
        if (!_tst(i)) {
            _set(i);
            return (void *)(i * PAGE_SIZE);
        }
    }
    return 0;  // out of memory
}

void pmm_free_page(void *page) {
    uint64_t idx = (uint64_t)page / PAGE_SIZE;
    if (idx >= 256 && idx < TRACKED_PAGES)
        _clr(idx);
}

// ============================================================================
// Heap Allocator — free-list, kmalloc / kfree
// ============================================================================

typedef struct Block {
    uint32_t      size;    // usable bytes, excluding this header
    uint8_t       in_use;
    uint8_t       _pad[3];
    struct Block *next;
} Block;

static Block *heap_head;

static void heap_expand(void) {
    void *page = pmm_alloc_page();
    if (!page) return;
    Block *b  = (Block *)page;
    b->size   = PAGE_SIZE - sizeof(Block);
    b->in_use = 0;
    b->next   = 0;
    if (!heap_head) { heap_head = b; return; }
    Block *cur = heap_head;
    while (cur->next) cur = cur->next;
    cur->next = b;
}

void *kmalloc(size_t size) {
    if (!size) return 0;
    size = (size + 7) & ~(size_t)7;  // align to 8 bytes

    for (int attempt = 0; attempt < 2; attempt++) {
        for (Block *b = heap_head; b; b = b->next) {
            if (b->in_use || b->size < size) continue;
            if (b->size >= size + sizeof(Block) + 8) {
                Block *tail  = (Block *)((uint8_t *)b + sizeof(Block) + size);
                tail->size   = b->size - (uint32_t)size - sizeof(Block);
                tail->in_use = 0;
                tail->next   = b->next;
                b->next = tail;
                b->size = (uint32_t)size;
            }
            b->in_use = 1;
            return (uint8_t *)b + sizeof(Block);
        }
        heap_expand();
    }
    return 0;
}

void kfree(void *ptr) {
    if (!ptr) return;
    Block *b  = (Block *)((uint8_t *)ptr - sizeof(Block));
    b->in_use = 0;
    while (b->next && !b->next->in_use) {
        b->size += sizeof(Block) + b->next->size;
        b->next  = b->next->next;
    }
}
