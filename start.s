format ELF64

section '.text' executable
  public binary_interface
  extrn main

; Microsoft binary interface rcx, rdx, r8, r9
; SystemV binary interface rdi, rsi , rdx, r10, r8  r9	
; rcx -> rdi (frame_buffer)
; rdx -> rsi (acpi_table)
binary_interface:
    mov rdi,rcx
    mov rsi,rdx
    mov rdx,r8
    mov r10,r9

    mov rsp, kernel_stack_top

    call main 



; if kernel return hang
.hang:
    cli
    hlt
    jmp .hang

; Reserve 16 KB for the kernel execution stack
section '.bss' writable align 16
  rb 16384
kernel_stack_top:

