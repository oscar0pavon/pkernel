#include "shell.h"
#include "types.h"
#include "input.h"
#include "console.h"
#include "memory.h"
#include "lapic_timer.h"
#include "sched.h"
#include "drivers/pci.h"
#include "drivers/nvme.h"
#include "block.h"
#include "gpt.h"
#include "acpi.h"
#include "input_output.h"


#define LINE_MAX 256

static char line[LINE_MAX];
static int  line_len = 0;

static int str_eq(const char *a, const char *b) {
    while (*a && *b) if (*a++ != *b++) return 0;
    return *a == *b;
}

// If `line` begins with `prefix`, return a pointer to the first non-space
// character after it (the argument); otherwise return NULL.
static const char *arg_after(const char *line, const char *prefix) {
    while (*prefix) if (*line++ != *prefix++) return 0;
    while (*line == ' ') line++;
    return line;
}

static void cmd_help(void) {
    printf("commands: help  clear  mem  uptime  tasks  lspci  nvme  lsblk  parts  reboot  poweroff\n");
    printf("          wtest <dev>   (non-destructive block write self-test)\n");
}

// Non-destructive write verification for a named block device.
//   1. read the original sector and keep it
//   2. write a known pattern, read it back, compare
//   3. write the original sector back to restore it
// The sector is restored either way, so no data is lost on success. It still
// WRITES to the device, so it must be invoked with an explicit device name and
// never runs by accident. On bare metal nvme0/nvme1 are REAL disks.
static void cmd_wtest(const char *dev_name) {
    if (!dev_name || dev_name[0] == '\0') {
        printf("usage: wtest <dev>   e.g. wtest nvme0\n");
        return;
    }
    BlockDevice *bd = block_get(dev_name);
    if (!bd)        { printf("wtest: no such block device '%s'\n", dev_name); return; }
    if (!bd->write) { printf("wtest: %s is read-only\n", bd->name); return; }
    if (bd->sector_size > 4096) { printf("wtest: sector too large\n"); return; }

    // Scratch LBA in the alignment gap between the GPT partition entry array
    // (ends at LBA 33) and the first partition (LBA 2048 on a 1 MiB-aligned
    // disk). This sector is normally unused, so even though the test restores
    // it afterwards, a worst-case interruption is least likely to lose data.
    uint64_t lba = 2047;
    if (lba >= bd->sector_count) lba = bd->sector_count - 1;
    uint32_t sz  = bd->sector_size;
    static uint8_t orig[4096], patt[4096], back[4096];

    printf("wtest: %s LBA %lu (writes + restores 1 sector)...\n", bd->name, lba);

    if (block_read(bd, lba, 1, orig) != 0) { printf("wtest: initial read failed\n"); return; }
    for (uint32_t i = 0; i < sz; i++) patt[i] = (uint8_t)((i * 7 + 0x5A) & 0xFF);

    if (block_write(bd, lba, 1, patt) != 0) { printf("wtest: write failed\n"); return; }
    if (block_read(bd, lba, 1, back) != 0)  { printf("wtest: readback failed\n"); return; }

    int ok = 1;
    for (uint32_t i = 0; i < sz; i++) if (back[i] != patt[i]) { ok = 0; break; }

    if (block_write(bd, lba, 1, orig) != 0)
        printf("wtest: WARNING — restore write failed, LBA %lu may be altered!\n", lba);

    printf("wtest: %s\n", ok ? "PASS (write/readback matched, sector restored)"
                             : "FAIL (readback did not match)");
}

static void cmd_lsblk(void) {
    if (block_device_count == 0) { printf("no block devices\n"); return; }
    for (int i = 0; i < block_device_count; i++) {
        BlockDevice *d = &block_devices[i];
        uint64_t mb = (d->sector_count / 1024) * d->sector_size / 1024;
        printf("%s  %lu MB  (%lu x %u B)  %s\n",
               d->name, mb, d->sector_count, d->sector_size,
               d->write ? "rw" : "ro");
    }
}

static void cmd_parts(void) {
    if (block_device_count == 0) { printf("no block devices\n"); return; }
    static GptPartition parts[16];
    for (int i = 0; i < block_device_count; i++) {
        BlockDevice *d = &block_devices[i];
        int n = gpt_scan(d, parts, 16);
        if (n < 0) { printf("%s: no GPT\n", d->name); continue; }
        printf("%s: %d partition(s)\n", d->name, n);
        for (int j = 0; j < n; j++) {
            GptPartition *p = &parts[j];
            uint64_t sectors = p->last_lba - p->first_lba + 1;
            uint64_t mb = (sectors / 1024) * d->sector_size / 1024;
            const char *type = gpt_type_name(p->type_guid);
            char guidbuf[37];
            if (!type) { gpt_guid_str(p->type_guid, guidbuf); type = guidbuf; }
            printf("  %sp%d  LBA %lu..%lu  %lu MB  %s  '%s'\n",
                   d->name, p->index + 1, p->first_lba, p->last_lba,
                   mb, type, p->name);
        }
    }
}


static void cmd_poweroff(void) {
    asm volatile("cli");

    printf("Goodbye\n");
    if (FADT && FADT->PM1aControlBlock) {
        output(power_manager.poweroff, (uint16_t)FADT->PM1aControlBlock);
        if (FADT->PM1bControlBlock)
            output(power_manager.poweroff, (uint16_t)FADT->PM1bControlBlock);
    }

    printf("Can't shutdown\n");
    asm volatile("sti");
}

static void cmd_reboot(void) {
    asm volatile("cli");

    printf("Restarting\n");
    // ACPI reset: write ResetValue to the FADT reset register (I/O port)
    if (FADT && FADT->ResetReg.address_space == 1)
        output_byte((uint8_t)FADT->ResetValue, (uint16_t)FADT->ResetReg.address);
    // Fallback: pulse CPU reset line via PS/2 controller
    output_byte(0xFE, 0x64);
    while (1) asm volatile("hlt");
}

static void cmd_nvme(void) {
    if (nvme_drive_count == 0) {
        printf("no NVMe drives found\n");
        return;
    }
    for (int i = 0; i < nvme_drive_count; i++) {
        NvmeDrive *d = &nvme_drives[i];
        if (!d->ready) { printf("nvme%d: not ready\n", i); continue; }
        uint64_t mb = (d->sector_count / 1024) * d->sector_size / 1024;
        printf("nvme%d: %s\n", i, d->model);
        printf("  serial:   %s\n", d->serial);
        printf("  firmware: %s\n", d->firmware);
        printf("  size:     %lu MB  (%lu sectors x %u B)\n",
               mb, d->sector_count, d->sector_size);

        // Read LBA 1 through the generic block layer — on a GPT disk this is
        // the GPT header, which begins with the ASCII signature "EFI PART".
        // Dumping it proves the DMA round-trip actually moved data (LBA 0's
        // first bytes are zero on a GPT disk, so they can't distinguish a real
        // read from an empty buffer). Going via block_read also exercises the
        // BlockDevice dispatch rather than calling the driver directly.
        static uint8_t sec1[512];
        BlockDevice *bd = &block_devices[i];
        if (block_read(bd, 1, 1, sec1) == 0) {
            printf("  LBA 1:   ");
            for (int j = 0; j < 16; j++) printf("%02x ", sec1[j]);
            printf("  '");
            for (int j = 0; j < 8; j++)
                printf("%s", (char[]){ (sec1[j] >= ' ' && sec1[j] <= '~')
                                       ? (char)sec1[j] : '.', '\0' });
            printf("'\n");
        }
    }
}

static void cmd_lspci(void) {
    static PciDevice devs[MAX_PCI_DEVICES];
    int count = get_pci_list(devs, MAX_PCI_DEVICES);
    for (int i = 0; i < count; i++) {
        PciDevice *d = &devs[i];
        printf("%02x:%02x.%x  %04x:%04x  class %02x:%02x:%02x\n",
               d->bus, d->device, d->function,
               d->vendor_id, d->device_id,
               d->class_code, d->subclass, d->prog_if);
    }
    printf("%d device(s) found\n", count);
}

static void cmd_tasks(void) {
    uint64_t now  = lapic_timer_uptime_ms();
    Task    *head = sched_task_list();
    Task    *t    = head;
    do {
        if (t->wake_time > now)
            printf("  [%d] %-8s  switches=%d  sleeping %d ms\n",
                   t->tid, t->name, (uint32_t)t->switches,
                   (uint32_t)(t->wake_time - now));
        else
            printf("  [%d] %-8s  switches=%d\n",
                   t->tid, t->name, (uint32_t)t->switches);
        t = t->next;
    } while (t != head);
}

static void cmd_uptime(void) {
    uint64_t ms = lapic_timer_uptime_ms();
    printf("%d.%d s (%d ticks)\n",
           (uint32_t)(ms / 1000),
           (uint32_t)(ms % 1000),
           (uint32_t)lapic_timer_get_ticks());
}

static void cmd_mem(void) {
    void *a = kmalloc(64);
    void *b = kmalloc(128);
    void *c = kmalloc(32);
    printf("kmalloc(64)  = 0x%lx\n", (uint64_t)a);
    printf("kmalloc(128) = 0x%lx\n", (uint64_t)b);
    printf("kmalloc(32)  = 0x%lx\n", (uint64_t)c);
    kfree(b);
    void *d = kmalloc(64);
    printf("kfree(b), kmalloc(64) = 0x%lx (reused b)\n", (uint64_t)d);
    kfree(a); kfree(c); kfree(d);
}

static void dispatch(void) {
    if (line_len == 0) return;
    line[line_len] = '\0';

    if      (str_eq(line, "help"))   cmd_help();
    else if (str_eq(line, "clear"))  console_clear();
    else if (str_eq(line, "mem"))    cmd_mem();
    else if (str_eq(line, "uptime")) cmd_uptime();
    else if (str_eq(line, "tasks"))  cmd_tasks();
    else if (str_eq(line, "lspci"))  cmd_lspci();
    else if (str_eq(line, "nvme"))   cmd_nvme();
    else if (str_eq(line, "lsblk"))  cmd_lsblk();
    else if (str_eq(line, "parts"))  cmd_parts();
    else if (arg_after(line, "wtest")) cmd_wtest(arg_after(line, "wtest"));
    else if (str_eq(line, "poweroff")) cmd_poweroff();
    else if (str_eq(line, "reboot")) cmd_reboot();
    else    printf("unknown: %s\n", line);
}

static void prompt(void) {
    printf("pkernel> ");
}

void shell_run(void) {
    prompt();
    while (1) {
        while (input_available()) {
            char c = input_getc();
            if (c == '\n') {
                printf("\n");
                dispatch();
                line_len = 0;
                prompt();
            } else if (c == '\b') {
                if (line_len > 0) {
                    line_len--;
                    console_backspace();
                }
            } else if (c >= ' ' && c <= '~' && line_len < LINE_MAX - 1) {
                line[line_len++] = c;
                char s[2] = {c, '\0'};
                printf("%s", s);
            }
        }
        asm volatile("hlt");
    }
}
