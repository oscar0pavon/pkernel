#ifndef __FAT32_H__
#define __FAT32_H__

#include "types.h"
#include "block.h"

// Read-only FAT32 driver layered on a BlockDevice. Handles 8.3 short names
// only; VFAT long-name (LFN) entries are skipped. Assumes 512-byte sectors.
//
// Layout knowledge lives in fat32_mount(); everything else works in terms of
// cluster numbers and the FAT chain, so it is independent of where the volume
// sits on the disk (raw or inside a GPT partition).

typedef struct {
  BlockDevice *dev;
  uint64_t part_lba;            // device LBA of this volume's boot sector
  uint32_t bytes_per_sector;    // validated == 512
  uint32_t sectors_per_cluster;
  uint32_t reserved_sectors;
  uint32_t num_fats;
  uint32_t fat_sectors;         // sectors per FAT
  uint32_t root_cluster;
  uint32_t first_data_sector;   // partition-relative LBA where cluster data begins
} Fat32Volume;

typedef struct {
  char     name[13];            // "NAME.EXT" (8.3, NUL-terminated)
  uint8_t  attr;
  uint32_t first_cluster;
  uint32_t size;                // bytes (0 for directories)
} Fat32DirEntry;

#define FAT_ATTR_DIRECTORY 0x10
#define FAT_ATTR_VOLUME_ID 0x08

// Mount the FAT32 volume whose boot sector is at `part_lba` on `dev`.
// Returns 0 on success, -1 if it is not a recognisable FAT32 volume.
int fat32_mount(BlockDevice *dev, uint64_t part_lba, Fat32Volume *vol);

// Resolve an absolute path ("/", "/dir", "/dir/file.txt") to a directory
// entry. Path components are matched case-insensitively against 8.3 names.
// Returns 0 on success, -1 if any component is not found.
int fat32_stat(Fat32Volume *vol, const char *path, Fat32DirEntry *out);

// Iterate the entries of the directory at `dir_cluster` (0 means the root),
// invoking `cb` for each real entry (deleted, LFN and volume-id entries are
// skipped). Iteration stops early if `cb` returns non-zero. Returns 0 on
// success, -1 on a read error. `cb` must not perform block I/O.
int fat32_list(Fat32Volume *vol, uint32_t dir_cluster,
               int (*cb)(const Fat32DirEntry *e, void *ctx), void *ctx);

// Read up to `max` bytes of the file beginning at `first_cluster` (whose total
// length is `size`) into `buf`. Returns the number of bytes read, or -1 on a
// read error.
int fat32_read(Fat32Volume *vol, uint32_t first_cluster, uint32_t size,
               void *buf, uint32_t max);

#endif
