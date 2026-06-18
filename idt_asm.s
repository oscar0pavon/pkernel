format ELF64

macro CALL_ALIGNED c_function {
    mov  rbp, rsp           ; Save RSP before alignment
    and  rsp, -16           ; Force 16-byte alignment
    call c_function         ; Execute the parameter passed to the macro
    mov  rsp, rbp           ; Restore unaligned RSP
}

macro PUSH_IRQ {
    push rax
    push rcx
    push rdx
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push rbp
}

macro POP_IRQ {
    pop  rbp
    pop  r11
    pop  r10
    pop  r9
    pop  r8
    pop  rdi
    pop  rsi
    pop  rdx
    pop  rcx
    pop  rax
}

section '.text' executable
  public exception_handler_0
  public exception_handler_13
  public exception_handler_14
  public load_idt_asm
  public irq_spurious_handler
  public irq_xhci_handler
  public irq_lapic_timer_handler
  public irq_sched_handler

  extrn c_exception_handler
  extrn usb_kbd_isr
  extrn lapic_timer_isr
  extrn sched_tick

load_idt_asm:
    lidt [rdi]
    ret

; Divide-by-zero doesn't push an error code, push a 0 placeholder
exception_handler_0:
    push 0          ; Error code placeholder
    push 0          ; Vector number
    jmp exception_common

; General Protection Fault (#GP) pushes an error code automatically
exception_handler_13:
    push 13         ; Vector number
    jmp exception_common

; Page Fault (#PF) pushes an error code automatically
exception_handler_14:
    push 14         ; Vector number
    jmp exception_common

; Spurious interrupts from LAPIC (vector 0xFF) — no EOI needed, just return
irq_spurious_handler:
    iretq


; xHCI MSI interrupt handler (vector 0x21)
; Saves all caller-saved registers (System V AMD64 ABI), aligns the stack to
; 16 bytes before the call (the interrupt frame + 10 pushes may or may not
; already be aligned depending on where the CPU was when the IRQ fired),
; calls the C handler, then unwinds.
irq_xhci_handler:
    PUSH_IRQ 
    CALL_ALIGNED usb_kbd_isr
    POP_IRQ
    iretq

; LAPIC timer periodic interrupt handler (vector 0x20)
irq_lapic_timer_handler:
    PUSH_IRQ
    CALL_ALIGNED lapic_timer_isr
    POP_IRQ
    iretq

; Preemptive scheduler handler (vector 0x20)
; Saves all 15 registers, passes saved-rsp to sched_tick(), switches to the
; returned rsp, then restores registers and iretqs into the next task.
; rdi is set to the frame pointer BEFORE and-aligning rsp, so sched_tick
; always receives the correct pointer to the r15 slot.
irq_sched_handler:
    push rax
    push rcx
    push rdx
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov  rdi, rsp           ; arg: pointer to saved-register frame (r15 slot)
    and  rsp, -16           ; align for C call (rdi already captured frame ptr)
    call sched_tick         ; returns new task's rsp in rax
    mov  rsp, rax           ; switch to new task's saved-register frame
    pop  r15
    pop  r14
    pop  r13
    pop  r12
    pop  rbx
    pop  rbp
    pop  r11
    pop  r10
    pop  r9
    pop  r8
    pop  rdi
    pop  rsi
    pop  rdx
    pop  rcx
    pop  rax
    iretq


exception_common:
    ; Clean alignment backup pass
    cld

    ; Extract stack frames according to System V ABI requirements
    mov rdi, [rsp]     ; Arg 1: Vector Number
    mov rsi, [rsp + 8] ; Arg 2: Error Code
    mov rdx, [rsp + 16]; Arg 3: Faulting Instruction Pointer (RIP)

    ; Call our C print debugger engine 
    call c_exception_handler
    
    ; Security fallback hang
.hang_loop:
    cli
    hlt
    jmp .hang_loop

