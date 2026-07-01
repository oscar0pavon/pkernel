format ELF64

section '.text' executable
  public binary_interface
  extrn main
  extrn _bss_start
  extrn _bss_end

; Microsoft binary interface rcx, rdx, r8, r9
; SystemV binary interface rdi, rsi , rdx, r10, r8  r9
; rcx -> rdi (frame_buffer)
; rdx -> rsi (acpi_table)
binary_interface:
    ; Preserve the BootInfo pointer (MS ABI arg0) across the .bss clear below.
    mov r15, rcx

    ; Zero the .bss section. C globals that rely on default zero-init (heap_head,
    ; counters, etc.) are only correct if .bss is actually zero. Firmware leaves
    ; RAM uninitialised on real hardware, so without this the first kmalloc walks
    ; a garbage heap_head and #GPs. QEMU hands us zeroed RAM, hiding the bug.
    ; This runs before the stack is set up and uses no stack, so clearing the
    ; stack region (which also lives in .bss) is safe.
    lea rdi, [_bss_start]
    lea rcx, [_bss_end]
    sub rcx, rdi
    xor eax, eax
    rep stosb

    ; Restore BootInfo into the SystemV arg0 register for main().
    mov rdi, r15

    mov rsp, kernel_stack_top
    and rsp, -16

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

