format ELF64

section '.text' executable
  public flush_tlb
  public update_cr3

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
