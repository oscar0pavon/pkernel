format ELF64
section '.text' executable
      public port_60
      public output
      public input

;SystemV binary interface rdi, rsi , rdx, r10, r8  r9	
port_60:
  in eax,60h
  ret

;di port
input:
  mov dx,di
  in eax,dx
  ret

;edi data
;si port
output:
  mov eax,edi;data
  mov dx,si;port
  out dx,eax
  ret

section '.data' writable
  input_data dd 0
  output_data dd 0

