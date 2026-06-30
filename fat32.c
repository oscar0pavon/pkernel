#include "fat32.h"
#include "block.h"

// FAT32 end-of-chain marker: any cluster value >= this terminates a chain.
#define FAT_EOC 0x0FFFFFF8u

// ---- little-endian readers over a raw byte buffer --------------------------

static uint32_t rd16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static uint32_t rd32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

// ---- cluster / FAT arithmetic ---------------------------------------------

// Partition-relative LBA of the first sector of a data cluster (>= 2).
static uint32_t cluster_lba(Fat32Volume *vol, uint32_t cluster) {
  return vol->first_data_sector + (cluster - 2) * vol->sectors_per_cluster;
}

// Follow the FAT to the cluster after `cluster`. Returns FAT_EOC on the last
// cluster or on a read error (both terminate iteration).
static uint32_t fat_next(Fat32Volume *vol, uint32_t cluster) {
  uint32_t fat_offset = cluster * 4;
  uint32_t sec = vol->reserved_sectors + (fat_offset / vol->bytes_per_sector);
  uint32_t off = fat_offset % vol->bytes_per_sector;
  uint8_t buf[512];
  if (block_read(vol->dev, vol->part_lba + sec, 1, buf) != 0) return FAT_EOC;
  return rd32(buf + off) & 0x0FFFFFFFu;
}

// ---- directory entry helpers ----------------------------------------------

// Flatten a raw 11-byte 8.3 field into "NAME.EXT" (uppercase, no padding).
static void format_83(const uint8_t *de, char *out) {
  int o = 0;
  for (int i = 0; i < 8; i++)
    if (de[i] != ' ') out[o++] = (char)de[i];
  if (de[8] != ' ' || de[9] != ' ' || de[10] != ' ') {
    out[o++] = '.';
    for (int i = 8; i < 11; i++)
      if (de[i] != ' ') out[o++] = (char)de[i];
  }
  out[o] = '\0';
}

static char up(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; }

static int name_ieq(const char *a, const char *b) {
  while (*a && *b) if (up(*a++) != up(*b++)) return 0;
  return *a == *b;
}

static void fill_entry(const uint8_t *de, const char *name, Fat32DirEntry *out) {
  int i = 0;
  for (; name[i]; i++) out->name[i] = name[i];
  out->name[i] = '\0';
  out->attr          = de[11];
  out->first_cluster = (rd16(de + 20) << 16) | rd16(de + 26);
  out->size          = rd32(de + 28);
}

// True for entries the higher layers should ignore (deleted / LFN / volume id).
static int entry_skip(const uint8_t *de) {
  if (de[0] == 0xE5) return 1;          // deleted
  if (de[11] == 0x0F) return 1;         // long-name (VFAT) component
  if (de[11] & FAT_ATTR_VOLUME_ID) return 1;
  return 0;
}

// ---- mount -----------------------------------------------------------------

int fat32_mount(BlockDevice *dev, uint64_t part_lba, Fat32Volume *vol) {
  uint8_t bs[512];
  if (block_read(dev, part_lba, 1, bs) != 0) return -1;
  if (bs[510] != 0x55 || bs[511] != 0xAA) return -1;   // boot signature

  uint32_t bps = rd16(bs + 11);
  if (bps != 512) return -1;                           // only 512 B sectors
  uint32_t spc = bs[13];
  if (spc == 0) return -1;
  uint32_t fat_sectors = rd32(bs + 36);                // BPB_FATSz32
  if (fat_sectors == 0) return -1;                     // 0 => FAT12/16, not us
  uint32_t root_cluster = rd32(bs + 44);
  if (root_cluster < 2) return -1;

  vol->dev                 = dev;
  vol->part_lba            = part_lba;
  vol->bytes_per_sector    = bps;
  vol->sectors_per_cluster = spc;
  vol->reserved_sectors    = rd16(bs + 14);
  vol->num_fats            = bs[16];
  vol->fat_sectors         = fat_sectors;
  vol->root_cluster        = root_cluster;
  vol->first_data_sector   = vol->reserved_sectors + vol->num_fats * fat_sectors;
  return 0;
}

// ---- directory scan --------------------------------------------------------

// Walk the directory chain starting at `cluster` (0 => root) looking for an
// entry whose 8.3 name matches `name`. Returns 0 and fills `out` on a hit, -1
// when the directory ends without a match or a read fails.
static int dir_find(Fat32Volume *vol, uint32_t cluster, const char *name,
                    Fat32DirEntry *out) {
  if (cluster == 0) cluster = vol->root_cluster;
  uint8_t buf[512];
  while (cluster >= 2 && cluster < FAT_EOC) {
    uint32_t base = cluster_lba(vol, cluster);
    for (uint32_t s = 0; s < vol->sectors_per_cluster; s++) {
      if (block_read(vol->dev, vol->part_lba + base + s, 1, buf) != 0) return -1;
      for (int e = 0; e < 512; e += 32) {
        uint8_t *de = buf + e;
        if (de[0] == 0x00) return -1;     // end of directory
        if (entry_skip(de)) continue;
        char fn[13];
        format_83(de, fn);
        if (name_ieq(fn, name)) { fill_entry(de, fn, out); return 0; }
      }
    }
    cluster = fat_next(vol, cluster);
  }
  return -1;
}

int fat32_list(Fat32Volume *vol, uint32_t cluster,
               int (*cb)(const Fat32DirEntry *, void *), void *ctx) {
  if (cluster == 0) cluster = vol->root_cluster;
  uint8_t buf[512];
  while (cluster >= 2 && cluster < FAT_EOC) {
    uint32_t base = cluster_lba(vol, cluster);
    for (uint32_t s = 0; s < vol->sectors_per_cluster; s++) {
      if (block_read(vol->dev, vol->part_lba + base + s, 1, buf) != 0) return -1;
      for (int e = 0; e < 512; e += 32) {
        uint8_t *de = buf + e;
        if (de[0] == 0x00) return 0;      // end of directory
        if (entry_skip(de)) continue;
        char fn[13];
        format_83(de, fn);
        Fat32DirEntry ent;
        fill_entry(de, fn, &ent);
        if (cb(&ent, ctx)) return 0;
      }
    }
    cluster = fat_next(vol, cluster);
  }
  return 0;
}

// ---- path resolution -------------------------------------------------------

int fat32_stat(Fat32Volume *vol, const char *path, Fat32DirEntry *out) {
  while (*path == '/') path++;
  if (*path == '\0') {                    // the root directory itself
    out->name[0]      = '/';
    out->name[1]      = '\0';
    out->attr         = FAT_ATTR_DIRECTORY;
    out->first_cluster = vol->root_cluster;
    out->size         = 0;
    return 0;
  }

  uint32_t cluster = vol->root_cluster;
  Fat32DirEntry e;
  while (*path) {
    char comp[13];
    int n = 0;
    while (*path && *path != '/' && n < 12) comp[n++] = *path++;
    comp[n] = '\0';
    while (*path == '/') path++;

    if (dir_find(vol, cluster, comp, &e) != 0) return -1;
    if (*path) {                          // more components follow
      if (!(e.attr & FAT_ATTR_DIRECTORY)) return -1;
      cluster = e.first_cluster ? e.first_cluster : vol->root_cluster;
    }
  }
  *out = e;
  return 0;
}

// ---- file read -------------------------------------------------------------

int fat32_read(Fat32Volume *vol, uint32_t first_cluster, uint32_t size,
               void *buf, uint32_t max) {
  uint32_t want = size < max ? size : max;
  uint32_t done = 0;
  uint32_t cluster = first_cluster;
  uint8_t  sec[512];
  uint8_t *out = buf;

  while (done < want && cluster >= 2 && cluster < FAT_EOC) {
    uint32_t base = cluster_lba(vol, cluster);
    for (uint32_t s = 0; s < vol->sectors_per_cluster && done < want; s++) {
      if (block_read(vol->dev, vol->part_lba + base + s, 1, sec) != 0) return -1;
      uint32_t chunk = want - done;
      if (chunk > 512) chunk = 512;
      for (uint32_t i = 0; i < chunk; i++) out[done + i] = sec[i];
      done += chunk;
    }
    cluster = fat_next(vol, cluster);
  }
  return (int)done;
}
