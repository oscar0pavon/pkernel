#include "block.h"
#include "console.h"

BlockDevice block_devices[MAX_BLOCK_DEVICES];
int block_device_count = 0;

static int name_eq(const char *a, const char *b) {
  while (*a && *b)
    if (*a++ != *b++)
      return 0;
  return *a == *b;
}

BlockDevice *
block_register(const char *name, uint32_t sector_size, uint64_t sector_count,
               uint32_t unit, void *drv,
               int (*read)(BlockDevice *, uint64_t, uint32_t, void *),
               int (*write)(BlockDevice *, uint64_t, uint32_t, const void *)) {

  if (block_device_count >= MAX_BLOCK_DEVICES) {
    printf("block: registry full, dropping %s\n", name);
    return NULL;
  }

  BlockDevice *d = &block_devices[block_device_count++];
  int i = 0;
  for (; name[i] && i < (int)sizeof(d->name) - 1; i++)
    d->name[i] = name[i];
  d->name[i] = '\0';
  d->sector_size = sector_size;
  d->sector_count = sector_count;
  d->unit = unit;
  d->drv = drv;
  d->read = read;
  d->write = write;

  printf("block: registered %s (%lu sectors x %u bytes, %s)\n", d->name,
         sector_count, sector_size, write ? "rw" : "ro");
  return d;
}

BlockDevice *block_get(const char *name) {
  for (int i = 0; i < block_device_count; i++)
    if (name_eq(block_devices[i].name, name))
      return &block_devices[i];
  return NULL;
}

int block_read(BlockDevice *dev, uint64_t lba, uint32_t count, void *buf) {
  if (!dev || !dev->read)
    return -1;
  if (count == 0)
    return 0;
  if (lba + count > dev->sector_count) {
    printf("block: %s read out of range (lba=%lu count=%u)\n", dev->name, lba,
           count);
    return -1;
  }
  return dev->read(dev, lba, count, buf);
}

int block_write(BlockDevice *dev, uint64_t lba, uint32_t count,
                const void *buf) {
  if (!dev || !dev->write)
    return -1; // read-only or invalid device
  if (count == 0)
    return 0;
  if (lba + count > dev->sector_count) {
    printf("block: %s write out of range (lba=%lu count=%u)\n", dev->name, lba,
           count);
    return -1;
  }
  return dev->write(dev, lba, count, buf);
}
