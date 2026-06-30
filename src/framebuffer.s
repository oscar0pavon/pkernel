format ELF64

section '.text' executable
  public fb_fill_dwords

; void fb_fill_dwords(void *dest, uint32_t value, uint64_t count)
;   rdi = dest, esi = value (32-bit pixel), rdx = count (number of dwords)
;
; Fills `count` 32-bit pixels at dest using non-temporal (movnti) stores, which
; bypass the cache and feed the CPU's write-combine buffers directly. Paired
; with a write-combining framebuffer mapping this gives near-bus-rate fills.
; A final sfence drains the WC buffers so the writes are globally visible.
fb_fill_dwords:
  test rdx, rdx
  jz .done

  ; Build a 64-bit pattern = value:value so we can store two pixels per movnti.
  mov eax, esi          ; zero-extends value into rax
  mov rcx, rax
  shl rcx, 32
  or rax, rcx           ; rax = (value << 32) | value

  ; Store the bulk as qwords (two pixels at a time).
  mov rcx, rdx
  shr rcx, 1            ; rcx = count / 2
  test rcx, rcx
  jz .tail
.qloop:
  movnti [rdi], rax
  add rdi, 8
  dec rcx
  jnz .qloop

.tail:
  test rdx, 1           ; trailing odd pixel?
  jz .fence
  movnti [rdi], eax     ; store the final single dword

.fence:
  sfence                ; drain write-combine buffers
.done:
  ret
