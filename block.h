#ifndef __BLOCK_H__
#define __BLOCK_H__

#include "types.h"

// Generic block-device layer. A driver (NVMe today, AHCI/USB-storage later)
// registers each unit it finds; higher layers (partition table parsing, a
// filesystem) talk to the BlockDevice interface and never touch the driver
// directly.

#define MAX_BLOCK_DEVICES 8

typedef struct BlockDevice {
  char     name[16];      // e.g. "nvme0"
  uint32_t sector_size;   // bytes per logical block
  uint64_t sector_count;  // total logical blocks
  uint32_t unit;          // driver sub-unit (e.g. NVMe drive index)
  void    *drv;           // driver-private context (optional)

  // Read `count` sectors starting at `lba` into `buf`. Returns 0 on success.
  int (*read)(struct BlockDevice *dev, uint64_t lba, uint32_t count, void *buf);
  // Write `count` sectors from `buf`. NULL if the device is read-only.
  int (*write)(struct BlockDevice *dev, uint64_t lba, uint32_t count,
               const void *buf);
} BlockDevice;

extern BlockDevice block_devices[MAX_BLOCK_DEVICES];
extern int         block_device_count;

// Register a block device. Returns the stored BlockDevice*, or NULL if the
// registry is full. `write` may be NULL for a read-only device.
BlockDevice *block_register(
    const char *name, uint32_t sector_size, uint64_t sector_count,
    uint32_t unit, void *drv,
    int (*read)(BlockDevice *, uint64_t, uint32_t, void *),
    int (*write)(BlockDevice *, uint64_t, uint32_t, const void *));

// Find a registered device by name (e.g. "nvme0"). NULL if not found.
BlockDevice *block_get(const char *name);

// Bounds-checked dispatch wrappers. Return 0 on success, -1 on error.
int block_read(BlockDevice *dev, uint64_t lba, uint32_t count, void *buf);
int block_write(BlockDevice *dev, uint64_t lba, uint32_t count, const void *buf);

#endif
