OUTPUT_FORMAT(binary)
ENTRY(binary_interface)
SECTIONS {
  . = 0x4000000;

  .text : {
    start.o(.text)
    *(.text*) 
  }
  .rodata : {
    *(.rodata*) 
  }
  .data : {
    *(.data*)
  }
  .bss : {
    *(.bss*)
  }
}

