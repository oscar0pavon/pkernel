OUTPUT_FORMAT(binary)
ENTRY(binary_interface)

SECTIONS {
  /* Enforce your absolute 64 MB physical memory base anchor */
  . = 0x4000000;

  .text : ALIGN(4096) {
    start_asm.o(.text)
    *(.text*) 
  }

  .rodata : ALIGN(4096) {
    *(.rodata*) 
  }

  .data : ALIGN(4096) {
    *(.data*)
  }

  .bss : ALIGN(4096) {
    /* Track the start of the BSS segment */
    _bss_start = .;
    *(.bss*)
    *(COMMON)
    _bss_end = .;
  }

  /* Mark the absolute physical memory termination boundary of the kernel */
  _kernel_end = .;
}

