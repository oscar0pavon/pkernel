OUTPUT_FORMAT(binary)
SECTIONS {
  . = 0x4000000;

  .text : {
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

