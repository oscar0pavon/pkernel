format ELF64

section '.text' executable
  public exception_handler_0
  public exception_handler_13
  public exception_handler_14
  public load_idt_asm

  extrn c_exception_handler

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

