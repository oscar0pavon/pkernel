format ELF64
section '.text' executable
      public port_60

port_60:
  in eax,60h
  ret


