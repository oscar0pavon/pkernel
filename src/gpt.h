#ifndef __GPT_H__
#define __GPT_H__

#include "types.h"
#include "block.h"

// GUID Partition Table parsing (UEFI spec 5.3). Works on any BlockDevice,
// so it is agnostic to the underlying storage driver.

#define GPT_MAX_PARTITIONS 128

typedef struct {
  int      index;            // entry index within the partition array
  uint64_t first_lba;
  uint64_t last_lba;
  uint64_t attributes;
  uint8_t  type_guid[16];    // all-zero entries are skipped (unused)
  uint8_t  unique_guid[16];
  char     name[37];         // UTF-16LE name flattened to ASCII (36 chars + NUL)
} GptPartition;

// Parse the GPT on `dev`, writing up to `max` used partitions into `out`.
// Returns the number of used partitions found, or -1 if there is no valid GPT.
int gpt_scan(BlockDevice *dev, GptPartition *out, int max);

// Return a human label for a partition type GUID, or NULL if unrecognised.
const char *gpt_type_name(const uint8_t type_guid[16]);

// Format a GUID as the canonical 8-4-4-4-12 string. `buf` needs >= 37 bytes.
void gpt_guid_str(const uint8_t guid[16], char *buf);

#endif
