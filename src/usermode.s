format ELF64

; ---------------------------------------------------------------------------
; Ring 3 entry/exit and the syscall/sysret fast system-call path.
;
; syscall/sysret (vs. an int-gate) don't use the IDT or the TSS: `syscall`
; jumps straight to IA32_LSTAR with CS/SS from IA32_STAR, and `sysret` reverses
; it. Neither instruction switches the stack pointer, so we do that by hand via
; a couple of saved-rsp globals below.
; ---------------------------------------------------------------------------

section '.text' executable
  public enter_user_mode
  public syscall_entry
  public return_to_kernel
  extrn  syscall_dispatch

; uint64_t enter_user_mode(uint64_t entry_rip, uint64_t user_stack_top)
;   rdi = entry point, rsi = top of the user stack.
; Drops to ring 3 with sysret. Only returns (rax = exit code) when the user
; program invokes SYS_EXIT, which comes back through return_to_kernel.
enter_user_mode:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    mov [kernel_saved_rsp], rsp   ; snapshot so return_to_kernel can unwind here

    mov ax, 0x1B                  ; user data selector (GDT entry 3 | RPL 3)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    cli                           ; no IRQ while rsp already points at the ring-3 stack
    mov rcx, rdi                  ; sysret loads RIP from rcx
    mov r11, 0x202                ; sysret loads RFLAGS from r11 (IF=1 -> IRQs back on in ring 3)
    mov rsp, rsi                  ; hand the user its own stack
    sysretq                       ; -> ring 3 at rcx (CS=0x23, SS=0x1B via STAR)

; IA32_LSTAR target: the CPU lands here on `syscall` from ring 3.
;   rcx = user return RIP, r11 = user RFLAGS  (both must survive for sysret)
;   rax = syscall number; rdi/rsi/rdx = args.
; RSP still points at the USER stack and FMASK has already cleared IF, so we
; switch to a kernel stack before doing anything that could fault or reenter.
syscall_entry:
    mov [user_saved_rsp], rsp
    mov rsp, [syscall_kernel_rsp]

    push rcx                      ; preserve user RIP across the C call
    push r11                      ; preserve user RFLAGS

    ; Marshal the ring-3 syscall regs into the SysV C ABI:
    ;   dispatch(num=rdi, a1=rsi, a2=rdx, a3=rcx)
    mov rcx, rdx                  ; a3 <- user rdx
    mov rdx, rsi                  ; a2 <- user rsi
    mov rsi, rdi                  ; a1 <- user rdi
    mov rdi, rax                  ; num <- user rax
    call syscall_dispatch         ; rax = return value

    pop r11
    pop rcx
    mov rsp, [user_saved_rsp]
    sysretq                       ; -> ring 3, rax carries the return value

; void return_to_kernel(uint64_t exit_code)   [does not return to its caller]
; Invoked by syscall_dispatch for SYS_EXIT. Throws away the ring-3/syscall
; context and unwinds straight back into enter_user_mode's caller, as though
; enter_user_mode had returned exit_code.
return_to_kernel:
    mov rsp, [kernel_saved_rsp]
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    mov cx, 0x10                  ; restore kernel data segments (were set to user 0x1B)
    mov ds, cx
    mov es, cx
    mov fs, cx
    mov gs, cx
    mov rax, rdi                  ; enter_user_mode() return value = exit code
    ret

section '.bss' writeable
  public kernel_saved_rsp
  public user_saved_rsp
  public syscall_kernel_rsp
kernel_saved_rsp    rq 1          ; enter_user_mode's frame, for the exit unwind
user_saved_rsp      rq 1          ; ring-3 rsp stashed across a syscall
syscall_kernel_rsp  rq 1          ; kernel stack the syscall handler switches to
