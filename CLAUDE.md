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

**pkernel** is a bare-metal x86_64 kernel targeting UEFI systems. It compiles to a raw position-independent binary (not ELF) loaded at physical address `0x4000000` (64 MB).

### Boot flow

1. **pboot** (separate UEFI EFI application, lives in `pboot/`) initializes the framebuffer, reads the UEFI memory map, locates the ACPI XSDT, loads the `pkernel` binary into memory, and jumps to it — passing a `BootInfo*` in `rcx` (Microsoft ABI, since it runs under UEFI).
2. **`start.s`** is the kernel entry point (`binary_interface`). It translates Microsoft ABI registers (`rcx`→`rdi`, `rdx`→`rsi`) to SystemV ABI, sets up a 16 KB stack, then calls `main()`.
3. **`main.c`** initializes subsystems in order: framebuffer → GDT → IDT → LAPIC → paging → ACPI → xHCI driver, then enters the `hlt` loop.

### Key structures

- **`BootInfo`** (`pkernel.h`) — the single argument passed from pboot to the kernel: contains `FrameBuffer`, `MemoryMapInfo`, and the ACPI XSDT physical address.
- **`FrameBuffer`** (`framebuffer.h`) — VRAM base address, resolution, and pitch; initialized first so `printf` works immediately.

### Module map

| File(s) | Role |
|---|---|
| `start.s` | ABI translation, stack setup, entry point |
| `main.c` | Kernel main — init sequencing |
| `types.h` | Type aliases: `byte`/`u8`/`u16`/`u32`/`u64`; `SYSVABI` attribute macro |
| `library.c/h` | `printf` and string utilities (no libc) |
| `console.c/h` | Cursor tracking, line scrolling |
| `framebuffer.c/h` | Pixel/character rendering into VRAM |
| `font.h` | Bitmap font data |
| `gdt.c/h` + `gdt_asm.s` | Global Descriptor Table setup and `lgdt` |
| `idt.c/h` + `idt_asm.s` | Interrupt Descriptor Table; exception/IRQ stubs |
| `lapic.c/h` | Local APIC init and EOI |
| `paging.c/h` | Kernel page tables |
| `acpi.c/h` | XSDT/MADT/MCFG/FADT parsing |
| `memory.c/h` | UEFI memory map parsing |
| `input_output.s/h` | `input_byte`/`output_byte` port I/O wrappers |
| `input.c/h` | Keyboard input buffering |
| `drivers/pci.c/h` | PCI config-space enumeration (I/O port method + PCIe MMIO via MCFG) |
| `drivers/xhci.c/h` | xHCI USB host controller driver; MSI-X interrupt; USB keyboard enumeration and HID polling |
| `drivers/ps2_keyboard.c/h` | Legacy PS/2 keyboard (IRQ 1) |

### Toolchain

- **Compiler**: `cc` (GCC) — `-ffreestanding -fno-stack-protector -mno-red-zone -fno-pic -mcmodel=kernel`
- **Assembler**: FASM (`./tools/fasm`) — used for `start.s`, `gdt_asm.s`, `idt_asm.s`, `input_output.s`
- **Linker**: `ld` with `binary.ld` — output format is raw binary, not ELF; `start.o` is placed first so `binary_interface` lands at the load address
- **Driver archive**: `drivers/` compiles to `drivers/drivers.a` (static archive) then linked into the final binary

### QEMU setup

`./run` boots with `-machine q35 -bios ./virtual_machine/uefi.bios` using a FAT virtual disk (`./virtual_machine/disk/`). An emulated xHCI controller (`-device qemu-xhci`) and USB keyboard (`-device usb-kbd`) are attached so the keyboard driver can be exercised.

### pboot (bootloader)

`pboot/` is a full UEFI EFI application built separately (`make` inside `pboot/`). Its `BootInfo` struct definition in `pboot/pkernel.h` must stay in sync with `pkernel.h` in the kernel root — both define the same `BootInfo` layout that crosses the boot/kernel boundary.
