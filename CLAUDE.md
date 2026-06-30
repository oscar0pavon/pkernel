# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Commands

```sh
# First-time setup: clone and build the pboot bootloader
./configure

# Build the kernel (produces the `pkernel` binary)
make -j4

# Run in QEMU (copies pkernel to VM disk, then boots via UEFI)
./run

# Clean build artifacts
make clean
```

There is no test suite. Testing is done by running in QEMU with `./run`.

## Architecture

**pkernel** is a bare-metal x86_64 kernel targeting UEFI systems. It compiles to a raw flat binary (not ELF) loaded at physical address `0x4000000` (64 MB).

### Boot flow

1. **pboot** (separate UEFI EFI application, lives in `pboot/`) initializes the framebuffer, reads the UEFI memory map, locates the ACPI XSDT, loads the `pkernel` binary into memory, and jumps to it — passing a `BootInfo*` in `rcx` (Microsoft ABI, since it runs under UEFI).
2. **`start.s`** is the kernel entry point (`binary_interface`). It zeroes `.bss`, translates Microsoft ABI (`rcx`→`rdi`) to SystemV ABI, sets up a 16 KB stack, then calls `main()`.
3. **`main.c`** initializes subsystems in this order: serial → framebuffer → ACPI (XSDT parse, CPU topology, power manager) → GDT → IDT → pmm → paging → LAPIC → LAPIC timer (PM-timer calibrated, 100 Hz periodic) → scheduler → IDT gates (`0x20` scheduler tick, `0x21` xHCI) → PCI → xHCI MSI + `sti`. `setup_pci()` is where storage comes up: it probes each NVMe controller it finds (`nvme_probe`), which registers a `BlockDevice` per drive. It then creates the `idle` and `shell` tasks (a `counter` task exists but is commented out) and `hang()`s in a `hlt` loop.

### Concurrency model

The kernel is **preemptively multitasked** (`sched.c` + `sched_asm.s`). The LAPIC timer fires IRQ `0x20` at 100 Hz; `irq_sched_handler` saves all registers onto the current task's stack, calls `sched_tick(rsp)` to pick the next runnable task in a circular list, and restores that task's frame. `main()`'s flow becomes the `main` task; `task_create(name, func)` spins up new tasks on `pmm`-allocated 4 KB stacks. `task_sleep(ms)` sets a `wake_time` and `hlt`s; sleeping tasks are skipped by the scheduler. Anything touching shared state across tasks must account for preemption.

### Key structures

- **`BootInfo`** (`pkernel.h`) — the single argument passed from pboot to the kernel: contains `FrameBuffer`, `MemoryMapInfo`, and the ACPI XSDT physical address.
- **`FrameBuffer`** (`framebuffer.h`) — VRAM base address, resolution, and pitch; initialized first so `printf` works immediately.
- **`Task`** (`sched.h`) — saved `rsp`, stack base, tid, `wake_time`, and `next` pointer in a circular run list.
- **`PowerManager`** (`acpi.h`) — holds pre-computed PM1a control block values for poweroff and reboot, filled during ACPI init.

### Memory

`memory.c` provides both a physical page allocator (`pmm_alloc_page`/`pmm_free_page`, 4 KB identity-mapped pages, seeded from the UEFI memory map via `pmm_init`) and a heap (`kmalloc`/`kfree`). There is no libc — all allocation goes through these. `paging_map_mmio(phys, size)` identity-maps MMIO regions discovered at runtime (e.g. PCI BARs above 4 GB) and flushes CR3.

### Storage stack

Three layers, each agnostic to the one below:

1. **`drivers/nvme.c`** — NVMe controller driver. `nvme_probe()` is called from `setup_pci()` for each NVMe PCI function; it sets up admin + I/O queues, identifies the namespace, and fills a `NvmeDrive`. `nvme_read`/`nvme_write` move sectors via a shared 4 KB DMA bounce buffer (`nvme_rw_buf`), so a single transfer is capped at 4096 bytes (`count * sector_size <= 4096`).
2. **`block.c`** — generic block-device layer. Drivers call `block_register(name, sector_size, sector_count, unit, drv, read, write)` (write may be `NULL` for read-only); higher layers use `block_get("nvme0")` then `block_read`/`block_write`. This is the seam where AHCI or USB mass-storage would plug in later.
3. **`gpt.c`** — GUID Partition Table parsing on top of any `BlockDevice`. `gpt_scan(dev, out, max)` returns the used partitions; `gpt_type_name`/`gpt_guid_str` are for display.
4. **`fat32.c`** — read-only FAT32 filesystem over a `BlockDevice` partition (8.3 short names; LFN entries skipped; 512-byte sectors assumed). `fat32_mount(dev, part_lba, vol)` parses the BPB; `fat32_stat` resolves a path; `fat32_list` iterates a directory; `fat32_read` reads a file by following the FAT chain. All cluster arithmetic is partition-relative, so it is agnostic to where the volume sits on disk.

The shell exercises all four: `lsblk` (registered devices), `nvme` (controller/namespace info), `parts` (GPT scan), `ro`/`rw`/`wtest` (read/write tests), and `ls`/`cat` (FAT32 — `fat_get()` in `shell.c` auto-mounts the first FAT32 volume by scanning each device's GPT partitions, falling back to a bare FAT32 at LBA 0).

### Module map

| File(s) | Role |
|---|---|
| `start.s` | ABI translation, `.bss` zero-init, stack setup, entry point |
| `main.c` | Kernel main — init sequencing |
| `types.h` | Type aliases: `byte`/`u8`/`u16`/`u32`/`u64`; `SYSVABI` attribute macro |
| `library.c/h` | `printf` and string utilities (no libc) |
| `console.c/h` | Cursor tracking, line scrolling |
| `framebuffer.c/h` | Pixel/character rendering into VRAM |
| `font.h` | Bitmap font data |
| `gdt.c/h` + `gdt_asm.s` | Global Descriptor Table setup and `lgdt` |
| `idt.c/h` + `idt_asm.s` | Interrupt Descriptor Table; exception/IRQ stubs |
| `lapic.c/h` | Local APIC init (`0xFEE00000`, identity-mapped) and EOI |
| `lapic_timer.c/h` | LAPIC timer; ACPI PM-timer calibrated periodic tick; `busy_wait_ms()` (safe before interrupts) |
| `sched.c/h` + `sched_asm.s` | Preemptive round-robin scheduler; context switch on IRQ `0x20` |
| `paging.c/h` | Kernel page tables; identity-maps 0–4 GB with 2 MB huge pages |
| `acpi.c/h` | XSDT/MADT/MCFG/FADT parsing; `PowerManager` for poweroff/reboot |
| `cpu.c/h` | MADT processor enumeration; `cpu_count` and `cpu_apic_ids[]` array |
| `memory.c/h` | UEFI memory map parsing; physical page allocator + `kmalloc`/`kfree` heap |
| `block.c/h` | Generic `BlockDevice` registry/dispatch layer between drivers and partition/FS code |
| `gpt.c/h` | GUID Partition Table parsing over any `BlockDevice` |
| `fat32.c/h` | Read-only FAT32 filesystem (8.3 names) over a `BlockDevice` partition |
| `shell.c/h` | Interactive shell task (`help`/`clear`/`mem`/`uptime`/`tasks`/`lspci`/`lsblk`/`nvme`/`parts`/`ls`/`cat`/`ro`/`rw`/`wtest`/`poweroff`/`reboot`/`cli`/`sti`/`hlt`) |
| `input_output.s/h` | `input(port)` / `output_byte(val, port)` port I/O wrappers |
| `input.c/h` | Keyboard input buffering |
| `drivers/pci.c/h` | PCI config-space enumeration (I/O port method + PCIe MMIO via MCFG); dispatches NVMe controllers to `nvme_probe` |
| `drivers/nvme.c/h` | NVMe controller driver; admin/IO queues, namespace identify, sector read/write via 4 KB bounce buffer; registers a `BlockDevice` per drive |
| `drivers/xhci.c/h` | xHCI USB host controller driver; MSI interrupt; USB keyboard enumeration and HID polling |
| `drivers/usb.c/h` | USB core: protocol constants and `usb_attach_device` class-driver dispatch (HID keyboard today; mass storage later) |
| `drivers/usb_keyboard.c/h` | USB HID keyboard scancode handling, feeds the input buffer |
| `drivers/ps2_keyboard.c/h` | Legacy PS/2 keyboard (IRQ 1) |
| `drivers/serial.c/h` | 16550 UART (COM1) output for early/debug logging |

### Toolchain

- **Compiler**: `cc` (GCC) — `-ffreestanding -fno-stack-protector -mno-red-zone -fno-pic -mcmodel=kernel`
- **Assembler**: FASM (`./tools/fasm`) — all `.s` files use FASM syntax (`format ELF64`, `use64`, etc.). Every root `*.s` is auto-assembled by the Makefile wildcard rule. Always write assembly in FASM syntax, never NASM or GAS.
- **Linker**: `ld` with `binary.ld` — output format is raw binary, not ELF; `start.o` is placed first so `binary_interface` lands at the load address
- **Driver archive**: `drivers/` compiles to `drivers/drivers.a` (static archive) then linked into the final binary

### QEMU setup

`./run` boots with `-enable-kvm -machine q35 -cpu host -smp 4 -bios ./virtual_machine/uefi.bios` using a FAT virtual disk (`./virtual_machine/disk/`, where `pkernel` is copied). An emulated xHCI controller (`-device qemu-xhci`) and USB keyboard (`-device usb-kbd`) are attached so the keyboard driver can be exercised, and an emulated NVMe disk (`-device nvme` backed by `./virtual_machine/nvme.img`) exercises the storage stack. `-serial stdio` routes COM1 output to the terminal.

### pboot (bootloader)

`pboot/` is a full UEFI EFI application built separately (`make` inside `pboot/`). Its `BootInfo` struct definition in `pboot/pkernel.h` must stay in sync with `pkernel.h` in the kernel root — both define the same `BootInfo` layout that crosses the boot/kernel boundary.
