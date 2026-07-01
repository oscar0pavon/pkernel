format ELF64

; ---------------------------------------------------------------------------
; The demo ring-3 program. It is linked into the kernel image only so its bytes
; can be COPIED into a user page at runtime; it never runs at this link address.
; Because it touches memory through no absolute address and uses only the
; syscall ABI (no privileged instructions), the exact same bytes execute
; correctly wherever the kernel maps them.
; ---------------------------------------------------------------------------

section '.text' executable
  public user_program_start
  public user_program_end

SYS_EXIT    = 0
SYS_PUTCHAR = 1

; print one character via the kernel: rax = SYS_PUTCHAR, rdi = char.
; `syscall` clobbers rcx and r11, so we reload rax/rdi every time.
macro putc ch {
    mov eax, SYS_PUTCHAR
    mov edi, ch
    syscall
}

user_program_start:
    putc 'H'
    putc 'e'
    putc 'l'
    putc 'l'
    putc 'o'
    putc ' '
    putc 'f'
    putc 'r'
    putc 'o'
    putc 'm'
    putc ' '
    putc 'r'
    putc 'i'
    putc 'n'
    putc 'g'
    putc ' '
    putc '3'
    putc '!'
    putc 10                 ; newline

    mov eax, SYS_EXIT
    mov edi, 42             ; exit code
    syscall

.spin:                      ; SYS_EXIT never returns; guard just in case
    jmp .spin
user_program_end:
