format ELF64

; ============================================================================
; AP (Application Processor) startup trampoline — RELOCATABLE.
;
; An AP always powers on in 16-bit REAL MODE, executing at physical
; (SIPI_vector << 12) with CS = vector<<8, IP = 0. smp_init() copies this blob
; to a usable low page (default 0x8000, but any 4 KB page < 1 MB works) and
; sends SIPI with the matching vector. The blob walks the core up:
; real mode -> 32-bit protected -> 64-bit long mode, then calls ap_main().
;
; POSITION INDEPENDENCE: nothing hardcodes the load address. The AP derives its
; own base at runtime from CS (base = CS << 4) and patches the two addresses it
; can't express relatively — the GDT-pointer base and the two far-jump targets —
; before using them. 64-bit code uses RIP-relative loads, which are already
; position independent. So the same bytes run correctly at any low page.
;
; Addressing rules:
;   - real mode: DS = CS, so [x - _start] resolves to base + offset.
;   - 32-bit:    segments are flat (base 0); EBP holds the linear base across
;                the mode switch, so [ebp + (x - _start)] reaches our data.
;   - 64-bit:    RIP-relative [x] — displacement is a same-section constant.
;
; The BSP patches three data slots before SIPI (addresses = base + offset):
;   ap_tramp_cr3   - kernel PML4 physical address (CR3) to install
;   ap_tramp_stack - top of this AP's private kernel stack
;   ap_tramp_entry - address of ap_main() to jump to in long mode
; ============================================================================

section '.text' executable
  public ap_trampoline_start
  public ap_trampoline_end
  public ap_tramp_cr3
  public ap_tramp_stack
  public ap_tramp_entry

use16
ap_trampoline_start:
_start:
    cli
    cld
    ; DS = CS so [x - _start] addresses base + offset (segment base = CS<<4).
    mov ax, cs
    mov ds, ax

    ; EBP = linear load base = CS << 4. Kept through protected mode for
    ; absolute data addressing; used now to fix up self-references.
    xor ebp, ebp
    mov bp, cs
    shl ebp, 4

    ; Patch the addresses we can't express relatively, all with the true base:
    ;   - the GDT pointer's base field
    ;   - the offset halves of the two indirect far-jump pointers
    lea eax, [ebp + (_gdt - _start)]
    mov [_gdt_ptr_base - _start], eax
    lea eax, [ebp + (_pm_entry - _start)]
    mov [_pm_far_off - _start], eax
    lea eax, [ebp + (_lm_entry - _start)]
    mov [_lm_far_off - _start], eax

    lgdt [_gdt_ptr - _start]

    ; Enter protected mode, then indirect far-jump (reloads CS = 0x08).
    mov eax, cr0
    or  al, 1
    mov cr0, eax
    jmp fword [_pm_far - _start]

use32
_pm_entry:
    ; Reload the data segments with the flat 32-bit data selector (0x10).
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    ; CR4.PAE — mandatory before enabling long mode.
    mov eax, cr4
    or  eax, 1 shl 5
    mov cr4, eax

    ; Install the kernel's page tables (identity-maps 0-4GB + the kernel), so
    ; this EIP and ap_main() stay mapped across the PG switch. EBP = base.
    mov eax, [ebp + (_cr3 - _start)]
    mov cr3, eax

    ; EFER.LME (IA32_EFER, MSR 0xC0000080, bit 8) — arm long mode.
    mov ecx, 0xC0000080
    rdmsr
    or  eax, 1 shl 8
    wrmsr

    ; CR0.PG — paging on. With PAE+LME set, the CPU is now in long mode
    ; (compatibility sub-mode until we far-jump to a 64-bit code segment).
    mov eax, cr0
    or  eax, 1 shl 31
    mov cr0, eax

    jmp fword [ebp + (_lm_far - _start)]

use64
_lm_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    ; Switch to this AP's private stack, then call into C. Both are patched by
    ; the BSP per-core just before SIPI. RIP-relative loads stay correct at any
    ; load address (the displacement is a same-section constant).
    mov rsp, [_stack]
    mov rax, [_entry]
    call rax

.halt:
    cli
    hlt
    jmp .halt

; ---------------------------------------------------------------------------
; Trampoline-local GDT: null / 32-bit code / 32-bit data / 64-bit code.
; All flat, base 0, limit 4 GB. ap_main() re-loads the real kernel GDT.
; ---------------------------------------------------------------------------
align 8
_gdt:
    dq 0x0000000000000000        ; 0x00 null
    dq 0x00CF9A000000FFFF        ; 0x08 code32: G=1 D=1, base 0, limit 0xFFFFF
    dq 0x00CF92000000FFFF        ; 0x10 data32: G=1 D=1, base 0, limit 0xFFFFF
    dq 0x00AF9A000000FFFF        ; 0x18 code64: G=1 L=1, base 0
_gdt_end:

_gdt_ptr:
    dw _gdt_end - _gdt - 1
_gdt_ptr_base:
    dd 0                         ; patched at runtime = base + (_gdt - _start)

; Indirect far-jump pointers (offset:selector). Offsets patched at runtime.
_pm_far:
_pm_far_off:  dd 0               ; base + (_pm_entry - _start)
              dw 0x08            ; 32-bit code selector
_lm_far:
_lm_far_off:  dd 0               ; base + (_lm_entry - _start)
              dw 0x18            ; 64-bit code selector

; ---- BSP-patched slots (see smp.c). Public alias + internal label share an
; ---- address; the internal one is used for the assembler arithmetic above.
align 8
ap_tramp_cr3:
_cr3:    dq 0
ap_tramp_stack:
_stack:  dq 0
ap_tramp_entry:
_entry:  dq 0

ap_trampoline_end:
