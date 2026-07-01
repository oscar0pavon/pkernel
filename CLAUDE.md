# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Commands

```sh
# First-time setup: clone and build the pboot bootloader (into ../pboot's repo)
./configure

# Build the kernel. The top-level Makefile just recurses into src/, which
# compiles the C objects, builds the asm/ and drivers/ archives, and links the
# raw binary at src/pkernel.
make

# Boot in QEMU (UEFI). Does NOT build — build first, then run.
./run

# Clean all build artifacts (recurses into src/asm and src/drivers)
make clean
```

There is no test suite. Testing is done by running in QEMU with `./run` and driving the interactive shell over the framebuffer/keyboard (and `-serial stdio` for logs).

## Build system layout

The tree is split into three compilation units that link together:

- **`src/*.c` + `src/*.h`** — kernel C. `src/Makefile` wildcard-compiles every `.c` to a `.o`.
- **`src/asm/*.s`** — FASM assembly, archived into `src/asm/assembly.a`. **Gotcha:** `assembly.a` is built by `ar rcs` over every `*_asm.o`, then `ar d assembly.a start_asm.o` deletes the entry point back out. `start_asm.o` is instead pulled in *directly* by the linker script (`asm/start_asm.o(.text)` in `binary.ld`) so `binary_interface` lands first at the load address; leaving it in the archive too would double-define it.
- **`src/drivers/*.c`** — driver C, archived into `src/drivers/drivers.a`.

Final link (in `src/Makefile`): `ld $(OBJS) ./asm/assembly.a ./drivers/drivers.a -T binary.ld -o pkernel`. Output format is **raw binary, not ELF** (`OUTPUT_FORMAT(binary)`), loaded at physical `0x4000000` (64 MB). Toolchain flags live in `src/include.Makefile` (shared by all three Makefiles): `cc -ffreestanding -fno-stack-protector -mno-red-zone -fno-pic -mcmodel=kernel`, assembler `../tools/fasm`. FASM writes its output to the exact object name passed as the second argument.

**Always write assembly in FASM syntax** (`format ELF64`, `use64`, `section '.text' executable`), never NASM or GAS.

## Architecture

**pkernel** is a bare-metal x86_64 kernel targeting UEFI systems, compiled to a flat binary.

### Boot flow

1. **pboot** (separate UEFI EFI application, its own repo cloned by `./configure`) sets up the framebuffer, reads the UEFI memory map, locates the ACPI XSDT, loads the `pkernel` binary, and jumps to it — passing a `BootInfo*` in `rcx` (Microsoft ABI, since it runs under UEFI).
2. **`src/asm/start.s`** is the kernel entry (`binary_interface`): zeroes `.bss` (bracketed by `_bss_start`/`_bss_end` in `binary.ld`), translates the Microsoft ABI (`rcx`→`rdi`) to SystemV, sets up the stack, and calls `main()`.
3. **`src/main.c`** initializes subsystems in order: serial → console/framebuffer → ACPI → GDT → IDT → pmm → paging → **usermode (TSS + syscall MSRs)** → LAPIC → LAPIC timer (100 Hz) → scheduler → IDT gates (`0x20` scheduler, `0x21` xHCI) → PCI (storage comes up in `setup_pci`) → xHCI MSI + `sti`. It then creates the `idle` and `shell` tasks and `hang()`s.

### Concurrency model

Preemptively multitasked (`sched.c` + `src/asm/sched.s`). The LAPIC timer fires IRQ `0x20` at 100 Hz; `irq_sched_handler` pushes all registers onto the current task's stack, calls `sched_tick(rsp)` to pick the next runnable task in a circular list, and restores that task's frame. `main()`'s flow becomes a task; `task_create(name, func)` spins up tasks on `pmm`-allocated 4 KB stacks; `task_sleep(ms)` sets a `wake_time` and `hlt`s (skipped by the scheduler while sleeping). Anything touching shared state across tasks must account for preemption.

### User mode (ring 3)

Ring-3 support uses the **`syscall`/`sysret`** fast-call pair (not an `int` gate), wired up in `usermode.c` + `src/asm/usermode.s`:

- **GDT ordering matters.** `sysret` derives the user selectors from `IA32_STAR[63:48]` as `SS = base+8`, `CS = base+16`, so `gdt.c` lists **user data (0x18) before user code (0x20)**. Swapping them breaks `sysret`. (Effective selectors: user data `0x1B`, user code `0x23`.)
- **TSS** (`gdt.c` entries 5–6, a 16-byte long-mode descriptor installed via `gdt_install_tss` + `ltr`). Only `rsp0` matters: the kernel stack the CPU loads when an interrupt fires while in ring 3. `syscall` does *not* use the TSS — it leaves `rsp` alone, so `syscall_entry` switches stacks by hand (`syscall_kernel_rsp`).
- **User pages.** `paging_map_user_page(va, phys)` maps 4 KB pages with `PAGE_USER` at *every* level of the walk (the CPU ANDs the U bit down the chain) into a dedicated high-half window (`USER_CODE_VA`/`USER_STACK_VA` under PML4 slot 8, which the kernel identity map never touches — so no kernel memory is exposed).
- **syscall ABI** (`usermode.h`): `rax` = number, args in `rdi/rsi/rdx`, return in `rax`. `SYS_EXIT` (0), `SYS_PUTCHAR` (1). `FMASK = 0x700` clears IF/DF/TF on entry.
- **Exit path caveat.** `SYS_EXIT` unwinds back to the kernel through `return_to_kernel` instead of `sysretq`, so it must **re-enable interrupts (`sti`) by hand** — `syscall` entry cleared IF via FMASK, and any handler exit that skips the matching return instruction has to restore IF itself or the kernel `hlt`s forever.
- The `user` shell command runs the bundled position-independent demo (`src/asm/user_program.s`), copied into a fresh user page by `run_user_program()`.

### Key structures

- **`BootInfo`** (`pkernel.h`) — the single argument from pboot: `FrameBuffer`, `MemoryMapInfo`, ACPI XSDT physical address. **Must stay in sync with pboot's own copy of the struct** across the boot/kernel boundary.
- **`FrameBuffer`** (`framebuffer.h`) — VRAM base, resolution, pitch; initialized first so `printf` works immediately.
- **`Task`** (`sched.h`) — saved `rsp`, stack base, tid, `wake_time`, `next` in a circular run list.
- **`PowerManager`** (`acpi.h`) — pre-computed PM1a values for poweroff/reboot, filled during ACPI init.

### Memory

`memory.c` provides both a physical page allocator (`pmm_alloc_page`/`pmm_free_page`, 4 KB identity-mapped pages, seeded from the UEFI memory map via `pmm_init`) and a heap (`kmalloc`/`kfree`). There is no libc. `paging.c` identity-maps 0–4 GB with 2 MB huge pages; `paging_map_mmio(phys, size)` identity-maps MMIO regions found at runtime (e.g. PCI BARs above 4 GB) and flushes the TLB. The framebuffer is re-tagged write-combining via PAT slot 4 (`setup_pat_wc`, using `read_msr`/`write_msr` from `src/asm/paging.s`). CR3/TLB/MSR helpers all live in `src/asm/paging.s`.

### Storage stack

Four layers, each agnostic to the one below:

1. **`drivers/nvme.c`** — NVMe driver. `nvme_probe()` (called from `setup_pci()` per NVMe function) sets up admin+I/O queues, identifies the namespace, registers a `BlockDevice`. Transfers go through a shared 4 KB DMA bounce buffer, so a single transfer is capped at 4096 bytes.
2. **`block.c`** — generic `BlockDevice` registry/dispatch (`block_register`/`block_get`/`block_read`/`block_write`; write may be `NULL` for read-only). The seam where AHCI or USB mass-storage would plug in.
3. **`gpt.c`** — GUID Partition Table parsing over any `BlockDevice`.
4. **`fat32.c`** — read-only FAT32 (8.3 names; LFN skipped; 512-byte sectors) over a partition. All cluster arithmetic is partition-relative.

The shell exercises all four (`lsblk`/`nvme`/`parts`/`wtest`/`ls`/`cat`); `fat_get()` in `shell.c` auto-mounts the first FAT32 volume by scanning each device's GPT.

### Module map

C sources/headers live in `src/`, assembly in `src/asm/`, drivers in `src/drivers/`.

| File(s) | Role |
|---|---|
| `asm/start.s` | ABI translation, `.bss` zero-init, stack setup, entry point (linked first, outside `assembly.a`) |
| `main.c` | Kernel main — init sequencing |
| `types.h` | Type aliases; `SYSVABI` attribute macro |
| `library.c/h` | `printf`, `memcpy`/`memset`, string utils (no libc) |
| `console.c/h`, `framebuffer.c/h` + `asm/framebuffer.s`, `font.h` | Console/cursor, VRAM rendering, non-temporal fast fill, font data |
| `gdt.c/h` + `asm/gdt.s` | GDT (incl. ring-3 + TSS descriptors), `lgdt`/`ltr` |
| `idt.c/h` + `asm/idt.s` | IDT; exception/IRQ stubs |
| `lapic.c/h`, `lapic_timer.c/h` | Local APIC; PM-timer-calibrated 100 Hz periodic tick; `busy_wait_ms()` |
| `sched.c/h` + `asm/sched.s` | Preemptive round-robin scheduler; context switch on IRQ `0x20` |
| `paging.c/h` + `asm/paging.s` | Kernel page tables (2 MB huge id-map), MMIO/user mapping, PAT-WC; CR3/TLB/MSR asm |
| `usermode.c/h` + `asm/usermode.s`, `asm/user_program.s` | Ring 3: TSS, syscall/sysret path, user-page mapping, demo program |
| `acpi.c/h`, `cpu.c/h` | XSDT/MADT/MCFG/FADT/DSDT parsing; `PowerManager`; MADT CPU enumeration |
| `memory.c/h` | UEFI memory map parsing; PMM + `kmalloc`/`kfree` heap |
| `block.c/h`, `gpt.c/h`, `fat32.c/h` | Storage layers (see above) |
| `shell.c/h` | Interactive shell task |
| `input.c/h`, `input_output.h` + `asm/input_output.s` | Keyboard input buffering; port I/O wrappers |
| `drivers/pci.c/h` | PCI/PCIe enumeration; dispatches NVMe to `nvme_probe` |
| `drivers/nvme.c/h`, `drivers/xhci.c/h`, `drivers/usb*.c/h`, `drivers/ps2_keyboard.c/h`, `drivers/serial.c/h` | NVMe, xHCI + USB core/HID keyboard, PS/2 keyboard, 16550 UART |

### QEMU setup

`./run` boots with `-enable-kvm -machine q35 -cpu host -smp 4 -bios ./virtual_machine/uefi.bios`, a FAT virtual disk (`./virtual_machine/disk/`), an emulated xHCI + USB keyboard, an emulated NVMe disk (`./virtual_machine/nvme.img`), and `-serial stdio` for COM1 logs.

### pboot (bootloader)

pboot is a separate UEFI EFI application in its own repository (cloned by `./configure`). Its `BootInfo` struct must stay layout-compatible with `pkernel.h` — both describe the same struct that crosses the boot/kernel boundary.
