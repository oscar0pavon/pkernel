format ELF64

section '.text' executable
  public binary_interface
  extrn main

;Microsoft binary interface rcx, rdx, r8, r9
;SystemV binary interface rdi, rsi , rdx, r10, r8  r9	
binary_interface:
    mov rdi,rcx
    mov rsi,rdx
    mov rdx,r8
    mov r10,r9
    call main 
    ret

