format ELF64

section '.text' executable
  public task_trampoline
  public task_exit

  extrn task_remove_current

; Entered via iretq for a brand-new task.
; r15 = the real task function pointer (set in the initial register frame).
task_trampoline:
    call r15        ; run the task function
    call task_exit  ; remove task and switch away if it returns

; void task_exit(void)
; Removes current task from the scheduler list and context-switches to the
; next task.  Never returns.
task_exit:
    cli
    and  rsp, -16
    call task_remove_current    ; C: splice out current, set current=next, return next->rsp
    mov  rsp, rax               ; switch to next task's saved-register frame
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
    iretq                       ; restores rip, cs, rflags, rsp, ss from next task's frame
