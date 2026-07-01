format ELF64

section '.text' executable
  public load_gdt
  public load_tss

; void load_tss(uint16_t selector)
; RDI holds the GDT selector for the TSS descriptor (0x28). ltr marks the TSS
; descriptor busy and points the CPU at it, so ring 3 -> ring 0 transitions know
; which kernel stack (TSS.RSP0) to switch to.
load_tss:
    ltr di
    ret

load_gdt:
    ; RDI holds the address of our 'gdt_ptr' struct (System V ABI convention)
    lgdt [rdi]

    ; 1. Reload the Data Segment registers with our Kernel Data Segment index
    ; Our Kernel Data descriptor is Entry 2 in the table. 2 * 8 bytes = 0x10
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; 2. Perform a Long Jump to update the Code Segment (CS) register
    ; Our Kernel Code descriptor is Entry 1 in the table. 1 * 8 bytes = 0x08
    ; In 64-bit mode, we push the target tracking parameters and execute a 'retfq'
    mov rax, 0x08                  ; Target segment descriptor offset (Kernel Code)
    push rax                       ; Push Code Segment target index onto stack
    lea rax, [.flush_pipeline]     ; Load address of our local continuation label
    push rax                       ; Push instruction pointer destination onto stack
    retfq                          ; Far Return forces the CPU to flush CS register state!

.flush_pipeline:
    ret

