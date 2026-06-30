format ELF64
section '.text' executable
      public port_60
      public output
      public input
      public input_byte
      public output_byte
      public clear_interptions

;SystemV binary interface rdi, rsi , rdx, r10, r8  r9	
port_60:
  in eax,60h
  ret

clear_interptions:
  cli
  ret

;di port
input:
  xor eax,eax
  mov dx,di
  in eax,dx
  ret

;di port
input_byte:
  mov dx,di
  in al,dx
  ret

;edi data
;si port
output_byte:
  mov ax,di;data
  mov dx,si;port
  out dx,ax
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

