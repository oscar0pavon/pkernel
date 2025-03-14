format ELF64
section '.text' executable
      public port_60
      public output
      public input

;SystemV binary interface rdi, rsi , rdx, r10, r8  r9	
port_60:
  in eax,60h
  ret

input:
  mov eax,edi
  mov dx,ax
  in eax,dx
  ret

output:
  mov eax,edi
  mov dx,ax
  out dx,eax
  ret

section '.data' writable
  input_data dd 0
  output_data dd 0

