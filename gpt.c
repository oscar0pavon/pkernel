#include "gpt.h"
#include "console.h"

// Little-endian readers (GPT stores all multi-byte integers little-endian).
static uint32_t rd32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t rd64(const uint8_t *p) {
  return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32);
}

// ============================================================================
// Known partition-type GUIDs, stored in their on-disk byte order. A GPT type
// GUID puts its first three fields little-endian and the last two big-endian,
// so these byte arrays are the raw 16 bytes as they appear on the medium.
// ============================================================================
struct GuidName { uint8_t guid[16]; const char *name; };

static const struct GuidName known_types[] = {
  // EFI System Partition  C12A7328-F81F-11D2-BA4B-00A0C93EC93B
  {{0x28,0x73,0x2A,0xC1, 0x1F,0xF8, 0xD2,0x11, 0xBA,0x4B,
    0x00,0xA0,0xC9,0x3E,0xC9,0x3B}, "EFI System"},
  // Microsoft basic data  EBD0A0A2-B9E5-4433-87C0-68B6B72699C7
  {{0xA2,0xA0,0xD0,0xEB, 0xE5,0xB9, 0x33,0x44, 0x87,0xC0,
    0x68,0xB6,0xB7,0x26,0x99,0xC7}, "MS basic data"},
  // Microsoft reserved    E3C9E316-0B5C-4DB8-817D-F92DF00215AE
  {{0x16,0xE3,0xC9,0xE3, 0x5C,0x0B, 0xB8,0x4D, 0x81,0x7D,
    0xF9,0x2D,0xF0,0x02,0x15,0xAE}, "MS reserved"},
  // Linux filesystem data 0FC63DAF-8483-4772-8E79-3D69D8477DE4
  {{0xAF,0x3D,0xC6,0x0F, 0x83,0x84, 0x72,0x47, 0x8E,0x79,
    0x3D,0x69,0xD8,0x47,0x7D,0xE4}, "Linux filesystem"},
  // Linux swap            0657FD6D-A4AB-43C4-84E5-0933C84B4F4F
  {{0x6D,0xFD,0x57,0x06, 0xAB,0xA4, 0xC4,0x43, 0x84,0xE5,
    0x09,0x33,0xC8,0x4B,0x4F,0x4F}, "Linux swap"},
  // Linux LVM             E6D6D379-F507-44C2-A23C-238F2A3DF928
  {{0x79,0xD3,0xD6,0xE6, 0x07,0xF5, 0xC2,0x44, 0xA2,0x3C,
    0x23,0x8F,0x2A,0x3D,0xF9,0x28}, "Linux LVM"},
};

const char *gpt_type_name(const uint8_t type_guid[16]) {
  for (unsigned k = 0; k < sizeof(known_types) / sizeof(known_types[0]); k++) {
    int match = 1;
    for (int b = 0; b < 16; b++)
      if (type_guid[b] != known_types[k].guid[b]) { match = 0; break; }
    if (match) return known_types[k].name;
  }
  return 0;
}

// ============================================================================
// Canonical GUID string: 8-4-4-4-12 with the mixed-endian field ordering UEFI
// uses for display (first three fields byte-reversed, last two as stored).
// ============================================================================
static char hex_digit(uint8_t v) { return v < 10 ? '0' + v : 'a' + (v - 10); }

static char *put_byte(char *out, uint8_t b) {
  *out++ = hex_digit(b >> 4);
  *out++ = hex_digit(b & 0xF);
  return out;
}

void gpt_guid_str(const uint8_t g[16], char *out) {
  out = put_byte(out, g[3]); out = put_byte(out, g[2]);
  out = put_byte(out, g[1]); out = put_byte(out, g[0]);
  *out++ = '-';
  out = put_byte(out, g[5]); out = put_byte(out, g[4]);
  *out++ = '-';
  out = put_byte(out, g[7]); out = put_byte(out, g[6]);
  *out++ = '-';
  out = put_byte(out, g[8]); out = put_byte(out, g[9]);
  *out++ = '-';
  for (int i = 10; i < 16; i++) out = put_byte(out, g[i]);
  *out = '\0';
}

// ============================================================================
// gpt_scan — read the GPT header at LBA 1, then walk the partition entry array.
// ============================================================================
int gpt_scan(BlockDevice *dev, GptPartition *out, int max) {
  if (!dev || max <= 0) return -1;

  static uint8_t hdr[512];
  if (block_read(dev, 1, 1, hdr) != 0) return -1;

  // Signature "EFI PART"
  static const char sig[8] = {'E','F','I',' ','P','A','R','T'};
  for (int i = 0; i < 8; i++) if (hdr[i] != (uint8_t)sig[i]) return -1;

  uint64_t array_lba = rd64(hdr + 72);
  uint32_t num_ent   = rd32(hdr + 80);
  uint32_t ent_size  = rd32(hdr + 84);
  if (ent_size < 128 || ent_size > 512) return -1;  // sane bounds
  if (num_ent > GPT_MAX_PARTITIONS) num_ent = GPT_MAX_PARTITIONS;

  uint32_t ssz = dev->sector_size;
  uint32_t spc = 4096 / ssz;            // sectors per 4 KB chunk
  if (spc == 0) spc = 1;
  uint32_t ent_per_chunk = (spc * ssz) / ent_size;
  if (ent_per_chunk == 0) return -1;

  static uint8_t buf[4096];
  int      found     = 0;
  uint32_t processed = 0;
  uint64_t lba       = array_lba;

  while (processed < num_ent && found < max) {
    if (block_read(dev, lba, spc, buf) != 0) break;

    for (uint32_t i = 0; i < ent_per_chunk && processed < num_ent && found < max;
         i++, processed++) {
      const uint8_t *e = buf + (uint64_t)i * ent_size;

      // Unused entry: type GUID is all zero.
      int used = 0;
      for (int b = 0; b < 16; b++) if (e[b]) { used = 1; break; }
      if (!used) continue;

      GptPartition *p = &out[found++];
      p->index = (int)processed;
      for (int b = 0; b < 16; b++) p->type_guid[b]   = e[b];
      for (int b = 0; b < 16; b++) p->unique_guid[b] = e[16 + b];
      p->first_lba  = rd64(e + 32);
      p->last_lba   = rd64(e + 40);
      p->attributes = rd64(e + 48);

      // Partition name: 36 UTF-16LE code units at offset 56. Flatten to ASCII.
      int c;
      for (c = 0; c < 36; c++) {
        uint16_t ch = (uint16_t)e[56 + c * 2] | ((uint16_t)e[56 + c * 2 + 1] << 8);
        if (ch == 0) break;
        p->name[c] = (ch >= ' ' && ch <= '~') ? (char)ch : '.';
      }
      p->name[c] = '\0';
    }
    lba += spc;
  }

  return found;
}
