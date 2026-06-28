#ifndef __NVME_H__
#define __NVME_H__

#include "../types.h"
#include <stdint.h>

// PCI identity
#define PCI_CLASS_STORAGE    0x01
#define PCI_SUBCLASS_NVM     0x08
#define PCI_PROGIF_NVME      0x02

#define NVME_MAX_DRIVES  4
#define NVME_QUEUE_DEPTH 64

// ============================================================================
// NVMe BAR0 Register Layout (NVMe 1.4 §3.1)
// ============================================================================
typedef struct {
  volatile uint64_t cap;   // 0x00 Controller Capabilities
  volatile uint32_t vs;    // 0x08 Version
  volatile uint32_t intms; // 0x0C Interrupt Mask Set
  volatile uint32_t intmc; // 0x10 Interrupt Mask Clear
  volatile uint32_t cc;    // 0x14 Controller Configuration
  volatile uint32_t rsvd;  // 0x18
  volatile uint32_t csts;  // 0x1C Controller Status
  volatile uint32_t nssr;  // 0x20 NVM Subsystem Reset
  volatile uint32_t aqa;   // 0x24 Admin Queue Attributes
  volatile uint64_t asq;   // 0x28 Admin SQ Base Address (page-aligned)
  volatile uint64_t acq;   // 0x30 Admin CQ Base Address (page-aligned)
} __attribute__((packed)) NvmeRegs;

// ============================================================================
// Submission Queue Entry — 64 bytes (NVMe 1.4 §4.6)
// ============================================================================
typedef struct {
  uint32_t cdw0;   // [7:0] opcode | [31:16] CID
  uint32_t nsid;
  uint64_t rsvd;
  uint64_t mptr;
  uint64_t prp1;   // first PRP entry (data start address)
  uint64_t prp2;   // second PRP entry (or PRP list pointer)
  uint32_t cdw10;
  uint32_t cdw11;
  uint32_t cdw12;
  uint32_t cdw13;
  uint32_t cdw14;
  uint32_t cdw15;
} __attribute__((packed)) NvmeSqe;

// ============================================================================
// Completion Queue Entry — 16 bytes (NVMe 1.4 §4.6)
// ============================================================================
typedef struct {
  uint32_t dw0;    // command-specific
  uint32_t dw1;    // reserved
  uint16_t sqhd;   // SQ head pointer (advances as controller drains SQ)
  uint16_t sqid;   // SQ identifier that generated this entry
  uint16_t cid;    // command identifier from the SQE
  uint16_t status; // bit 0 = phase tag; bits [15:1] = status field (0 = ok)
} __attribute__((packed)) NvmeCqe;

// ============================================================================
// Per-queue ring state
// ============================================================================
typedef struct {
  volatile NvmeSqe  *sq;
  volatile NvmeCqe  *cq;
  volatile uint32_t *sq_db; // SQ tail doorbell register
  volatile uint32_t *cq_db; // CQ head doorbell register
  uint32_t sq_tail;
  uint32_t cq_head;
  uint32_t phase;   // expected phase bit in next completion (starts at 1)
  uint16_t cid;     // next command identifier to assign
} NvmeQueue;

// ============================================================================
// Public drive descriptor
// ============================================================================
typedef struct NvmeDrive {
  uint64_t base_mmio;
  uint64_t sector_count;
  uint32_t sector_size;
  int      ready;
  char     model[41];
  char     serial[21];
  char     firmware[9];
} NvmeDrive;

extern int       nvme_drive_count;
extern NvmeDrive nvme_drives[NVME_MAX_DRIVES];

// 4 KB bounce buffer for NVMe DMA reads/writes
extern uint8_t nvme_rw_buf[4096];

// Called from setup_pci() for each NVMe controller found.
void nvme_probe(uint64_t mmio_base, volatile uint32_t *pci_regs);

// Read `count` sectors starting at `lba` into `buf`.
// Returns 0 on success, -1 on error. count * sector_size must be <= 4096.
int nvme_read(int drive, uint64_t lba, uint32_t count, void *buf);

#endif
