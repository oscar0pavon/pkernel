format ELF64
section '.text' executable
  extrn printf
  public create_base_address
  public convert32_to_64
;SystemV binary interface rdi, rsi , rdx, r10, r8  r9	

; bit 0 to 3 must to be zero
; al register is 8 bits
create_base_address:
  xor al,al
  mov al,0Ch;register offset
  shl al,2
  ret

  lea rdi,[message]
  mov rsi,45
  call printf
  lea rdi,[message2]
  call printf
  ret
  message2 db "message two",10,0
  
convert32_to_64:
  ret



section '.data' writable
  message db "My message %d",10,0

