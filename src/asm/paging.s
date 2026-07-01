format ELF64

section '.text' executable
  public flush_tlb
  public update_cr3
  public read_msr
  public write_msr

; Reload CR3 to flush the TLB (all non-global entries). Writing CR3 — even with
; the same value just read back — makes the CPU discard cached translations, so
; page-table entries edited in RAM (e.g. a freshly identity-mapped MMIO region)
; take effect on the next access instead of hitting a stale not-present entry.
flush_tlb:
  mov rax, cr3
  mov cr3, rax
  ret

update_cr3:
  mov cr3, rdi
  ret

; uint64_t read_msr(uint32_t msr)   ; edi = msr index
; rdmsr returns the 64-bit value in edx:eax (upper halves of rax/rdx cleared).
read_msr:
  mov ecx, edi
  rdmsr
  shl rdx, 32
  or rax, rdx
  ret

; void write_msr(uint32_t msr, uint64_t value)  ; edi = msr index, rsi = value
; wrmsr writes edx:eax to the MSR named in ecx.
write_msr:
  mov ecx, edi
  mov eax, esi          ; low 32 bits (zero-extends into rax)
  mov rdx, rsi
  shr rdx, 32           ; high 32 bits
  wrmsr
  ret
