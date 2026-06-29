#include "nvme.h"
#include "../memory.h"
#include "../paging.h"
#include "../console.h"
#include "../block.h"
#include <string.h>
#include <stdint.h>

// ============================================================================
// Global state
// ============================================================================

int       nvme_drive_count = 0;
NvmeDrive nvme_drives[NVME_MAX_DRIVES];

// Per-controller queue state. A machine can have several NVMe controllers
// (this box has two Kingstons on buses 04 and 06); each owns its own admin and
// I/O queues with their own doorbells, so this state MUST be per-controller.
// A single shared copy made every drive's reads land on the last one probed.
static NvmeQueue aq_q[NVME_MAX_DRIVES];   // Admin queue, per controller
static NvmeQueue ioq_q[NVME_MAX_DRIVES];  // I/O queue 1, per controller

// Per-controller DMA queue memory. Each queue base must be page-aligned and
// physically contiguous (PC=1); the aligned4k members give every queue its own
// 4 KB-aligned slot, and the array stride stays a multiple of 4 KB.
typedef struct {
  aligned4k NvmeSqe asq[NVME_QUEUE_DEPTH];   // admin submission (64 B entries)
  aligned4k NvmeCqe acq[NVME_QUEUE_DEPTH];   // admin completion (16 B entries)
  aligned4k NvmeSqe iosq[NVME_QUEUE_DEPTH];  // I/O submission
  aligned4k NvmeCqe iocq[NVME_QUEUE_DEPTH];  // I/O completion
} NvmeCtrlMem;
aligned4k static NvmeCtrlMem ctrl_mem[NVME_MAX_DRIVES];

// Shared scratch — only ever touched while serialised (probe runs once at boot,
// and the shell is the single NVMe caller), so one copy is fine.
aligned4k static uint8_t idbuf[4096];    // Identify response
aligned4k        uint8_t nvme_rw_buf[4096]; // Exported read/write bounce buffer

// ============================================================================
// Submit one command to a queue and poll until completion.
// Returns 0 on success, -1 on error or timeout.
// ============================================================================
static int nvme_cmd(NvmeQueue *q, uint8_t opc, uint32_t nsid,
                    uint64_t prp1, uint64_t prp2,
                    uint32_t cdw10, uint32_t cdw11, uint32_t cdw12) {
  uint16_t cid = q->cid++;
  volatile NvmeSqe *s = &q->sq[q->sq_tail];
  s->cdw0  = (uint32_t)opc | ((uint32_t)cid << 16);
  s->nsid  = nsid;
  s->rsvd  = 0;
  s->mptr  = 0;
  s->prp1  = prp1;
  s->prp2  = prp2;
  s->cdw10 = cdw10;
  s->cdw11 = cdw11;
  s->cdw12 = cdw12;
  s->cdw13 = 0;
  s->cdw14 = 0;
  s->cdw15 = 0;

  q->sq_tail = (q->sq_tail + 1) % NVME_QUEUE_DEPTH;
  *q->sq_db  = q->sq_tail;

  // Poll the CQ entry at cq_head for our phase bit (spec §4.6).
  // The controller sets the phase tag when it writes the completion.
  for (uint32_t i = 0; i < 5000000; i++) {
    volatile NvmeCqe *c = &q->cq[q->cq_head];
    if ((c->status & 1) != q->phase) { __asm__("pause"); continue; }

    uint16_t sf = c->status >> 1;   // status field; 0 = success
    q->cq_head = (q->cq_head + 1) % NVME_QUEUE_DEPTH;
    if (q->cq_head == 0) q->phase ^= 1;  // phase flips on head wrap
    *q->cq_db = q->cq_head;

    if (sf) {
      uint8_t sct = (sf >> 8) & 0x7;
      uint8_t sc  = sf & 0xFF;
      printf("nvme: cmd 0x%02x error SCT=%d SC=0x%02x\n", opc, sct, sc);
      return -1;
    }
    return 0;
  }
  printf("nvme: cmd 0x%02x timeout\n", opc);
  return -1;
}

// ============================================================================
// Copy an NVMe ASCII string, stripping trailing spaces.
// ============================================================================
static void str_trim(char *dst, const uint8_t *src, int len) {
  for (int i = 0; i < len; i++) dst[i] = (char)src[i];
  dst[len] = '\0';
  for (int i = len - 1; i >= 0 && dst[i] == ' '; i--) dst[i] = '\0';
}

// ============================================================================
// Block-layer adapter: bridges a BlockDevice back to nvme_read via the unit
// index stashed in dev->unit.
// ============================================================================
static int nvme_block_read(BlockDevice *dev, uint64_t lba, uint32_t count,
                           void *buf) {
  return nvme_read((int)dev->unit, lba, count, buf);
}

static int nvme_block_write(BlockDevice *dev, uint64_t lba, uint32_t count,
                            const void *buf) {
  return nvme_write((int)dev->unit, lba, count, buf);
}

// ============================================================================
// nvme_probe — called from pci.c for each NVMe PCI function found.
// Initialises the controller, sets up admin + I/O queues, identifies the
// first namespace, and registers the drive in nvme_drives[].
// ============================================================================
void nvme_probe(uint64_t mmio_base, volatile uint32_t *pci_regs) {
  if (nvme_drive_count >= NVME_MAX_DRIVES) return;

  int          idx  = nvme_drive_count;  // this controller's slot
  NvmeQueue   *aq   = &aq_q[idx];
  NvmeQueue   *ioq  = &ioq_q[idx];
  NvmeCtrlMem *mem  = &ctrl_mem[idx];

  // NVMe MMIO covers at minimum: registers (0x0–0x3F) + doorbells at 0x1000.
  // Map 64 KB to be safe for controllers with a large doorbell stride.
  paging_map_mmio(mmio_base, 0x10000);
  volatile NvmeRegs *regs = (volatile NvmeRegs *)mmio_base;

  uint64_t cap   = regs->cap;
  uint32_t dstrd = (uint32_t)((cap >> 32) & 0xF); // doorbell stride selector
  uint32_t to    = (uint32_t)((cap >> 24) & 0xFF); // timeout in 500 ms units
  uint32_t mqes  = (uint32_t)(cap & 0xFFFF);       // max queue entries - 1
  (void)to;

  uint32_t qdepth = NVME_QUEUE_DEPTH;
  if (qdepth > mqes + 1) qdepth = mqes + 1;

  uint32_t stride = 4U << dstrd;  // bytes between doorbell registers

  printf("nvme: CAP=0x%lx VS=0x%x dstrd=%d mqes=%d\n",
         cap, regs->vs, dstrd, mqes);

  // Enable PCI Memory Space + Bus Mastering so DMA works.
  pci_regs[1] |= (1U << 1) | (1U << 2);

  // ---- Disable controller (EN=0) and wait for RDY=0 ----
  regs->cc &= ~1U;
  for (uint32_t i = 0; i < 2000000; i++) {
    if (!(regs->csts & 1)) break;
    __asm__("pause");
  }
  if (regs->csts & 1) {
    printf("nvme: controller did not disable (CSTS=0x%x)\n", regs->csts);
    return;
  }

  // ---- Set up Admin queues ----
  memset(mem->asq, 0, sizeof(mem->asq));
  memset(mem->acq, 0, sizeof(mem->acq));
  regs->asq = (uint64_t)mem->asq;
  regs->acq = (uint64_t)mem->acq;
  // AQA: [11:0] ASQS = qdepth-1 | [27:16] ACQS = qdepth-1
  regs->aqa = ((qdepth - 1) << 16) | (qdepth - 1);

  // Wire admin queue doorbell pointers (doorbells start at base + 0x1000).
  // Admin SQ Tail = base + 0x1000 + 0*stride
  // Admin CQ Head = base + 0x1000 + 1*stride
  uint8_t *db = (uint8_t *)mmio_base + 0x1000;
  aq->sq     = mem->asq;
  aq->cq     = mem->acq;
  aq->sq_db  = (volatile uint32_t *)(db + 0 * stride);
  aq->cq_db  = (volatile uint32_t *)(db + 1 * stride);
  aq->sq_tail = 0;
  aq->cq_head = 0;
  aq->phase   = 1;
  aq->cid     = 0;

  // ---- Enable controller ----
  // CC: IOSQES=6 (2^6=64 B), IOCQES=4 (2^4=16 B), MPS=0 (4 KB), EN=1
  regs->cc = (6U << 16) | (4U << 20) | 1U;

  for (uint32_t i = 0; i < 2000000; i++) {
    uint32_t csts = regs->csts;
    if (csts & 2) { printf("nvme: fatal status (CSTS=0x%x)\n", csts); return; }
    if (csts & 1) break;
    __asm__("pause");
  }
  if (!(regs->csts & 1)) {
    printf("nvme: controller did not become ready (CSTS=0x%x)\n", regs->csts);
    return;
  }
  printf("nvme: controller ready\n");

  // ---- Identify Controller (CNS=0x01) ----
  memset(idbuf, 0, sizeof(idbuf));
  if (nvme_cmd(aq, 0x06, 0, (uint64_t)idbuf, 0, 0x01, 0, 0) != 0) return;

  NvmeDrive *d = &nvme_drives[idx];
  d->base_mmio = mmio_base;
  str_trim(d->serial,   idbuf + 4,  20);
  str_trim(d->model,    idbuf + 24, 40);
  str_trim(d->firmware, idbuf + 64, 8);
  printf("nvme: model='%s' serial='%s' fw='%s'\n",
         d->model, d->serial, d->firmware);

  // ---- Create I/O Completion Queue (QID=1) ----
  memset(mem->iocq, 0, sizeof(mem->iocq));
  ioq->cq     = mem->iocq;
  // I/O CQ 1 Head doorbell: base + 0x1000 + 3*stride
  ioq->cq_db  = (volatile uint32_t *)(db + 3 * stride);
  ioq->cq_head = 0;
  ioq->phase   = 1;

  // CDW10: QID=1 | (QSIZE-1)<<16
  // CDW11: PC=1 (physically contiguous), IEN=0 (polling, no MSI needed)
  if (nvme_cmd(aq, 0x05, 0, (uint64_t)mem->iocq, 0,
               1U | ((qdepth - 1) << 16), 1U, 0) != 0) return;

  // ---- Create I/O Submission Queue (QID=1, CQID=1) ----
  memset(mem->iosq, 0, sizeof(mem->iosq));
  ioq->sq     = mem->iosq;
  // I/O SQ 1 Tail doorbell: base + 0x1000 + 2*stride
  ioq->sq_db  = (volatile uint32_t *)(db + 2 * stride);
  ioq->sq_tail = 0;
  ioq->cid     = 0;

  // CDW10: QID=1 | (QSIZE-1)<<16
  // CDW11: PC=1 | QPRIO=0 | CQID=1<<16
  if (nvme_cmd(aq, 0x01, 0, (uint64_t)mem->iosq, 0,
               1U | ((qdepth - 1) << 16), 1U | (1U << 16), 0) != 0) return;

  // ---- Identify Namespace 1 (CNS=0x00) ----
  memset(idbuf, 0, sizeof(idbuf));
  if (nvme_cmd(aq, 0x06, 1, (uint64_t)idbuf, 0, 0x00, 0, 0) != 0) return;

  // NSZE at bytes [7:0] — namespace size in logical blocks
  uint64_t nsze = *(uint64_t *)idbuf;
  // FLBAS byte 26 bits [3:0] — index into the LBAF table
  uint8_t flbas = idbuf[26] & 0xF;
  // LBAF table at byte 128, each entry 4 bytes; byte 2 of entry = LBADS (2^n bytes)
  uint8_t lbads = idbuf[128 + flbas * 4 + 2];
  uint32_t blksz = 1U << lbads;

  d->sector_count = nsze;
  d->sector_size  = blksz;
  d->ready        = 1;
  nvme_drive_count++;

  uint64_t mb = (nsze >> 20) * blksz + ((nsze & 0xFFFFF) * blksz >> 20);
  printf("nvme%d: %lu sectors x %u bytes = %lu MB\n", idx, nsze, blksz, mb);

  // Expose this namespace through the generic block layer. NVME_MAX_DRIVES <= 9,
  // so a single digit suffix is sufficient.
  char name[6] = {'n', 'v', 'm', 'e', (char)('0' + idx), '\0'};
  block_register(name, blksz, nsze, (uint32_t)idx, NULL,
                 nvme_block_read, nvme_block_write);
}

// ============================================================================
// Read `count` contiguous logical blocks starting at `lba`.
// count * sector_size must fit within the 4 KB bounce buffer.
// ============================================================================
int nvme_read(int drive, uint64_t lba, uint32_t count, void *buf) {
  if (drive < 0 || drive >= nvme_drive_count || !nvme_drives[drive].ready)
    return -1;

  uint32_t nb = count * nvme_drives[drive].sector_size;
  if (nb > sizeof(nvme_rw_buf)) {
    printf("nvme_read: request too large (%u bytes)\n", nb);
    return -1;
  }

  // NVMe Read: opcode=0x02 — issue on this drive's own I/O queue.
  // CDW10 = LBA[31:0], CDW11 = LBA[63:32], CDW12 = NLB (0-based count)
  int r = nvme_cmd(&ioq_q[drive], 0x02, 1,
                   (uint64_t)nvme_rw_buf, 0,
                   (uint32_t)lba, (uint32_t)(lba >> 32), count - 1);
  if (r == 0) memcpy(buf, (void *)nvme_rw_buf, nb);
  return r;
}

// ============================================================================
// Write `count` contiguous logical blocks starting at `lba` from `buf`.
// count * sector_size must fit within the 4 KB bounce buffer.
// ============================================================================
int nvme_write(int drive, uint64_t lba, uint32_t count, const void *buf) {
  if (drive < 0 || drive >= nvme_drive_count || !nvme_drives[drive].ready)
    return -1;

  uint32_t nb = count * nvme_drives[drive].sector_size;
  if (nb > sizeof(nvme_rw_buf)) {
    printf("nvme_write: request too large (%u bytes)\n", nb);
    return -1;
  }

  // Stage the caller's data in the DMA-capable bounce buffer, then issue the
  // write so the controller reads from a known identity-mapped address.
  memcpy((void *)nvme_rw_buf, buf, nb);

  // NVMe Write: opcode=0x01 (same CDW layout as Read), on this drive's queue.
  return nvme_cmd(&ioq_q[drive], 0x01, 1,
                  (uint64_t)nvme_rw_buf, 0,
                  (uint32_t)lba, (uint32_t)(lba >> 32), count - 1);
}
