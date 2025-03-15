format ELF64
section '.text' executable
      public get_hex_string

;SystemV binary interface rdi, rsi , rdx, r10, r8  r9	


;rdi value
;rsi amount of bits 
;rdx destination buffer
;rax amount of bytes written to output buffer
get_hex_string:
	mov rsi,64
	lea rdx,[buffer]
	xor rax,rax
	shr rsi,2 ;divide by 4
	add rdx,rsi
	nibble:
		lea r9,[hex_table]
		mov bl,dil
		and bl,0x0f
		add r9b,bl
		mov bl,[r9]
		sub rdx,1
		mov byte [rdx],bl
		shr rdi,4
		add rax,1
		cmp rax,rsi
		jl nibble

	lea rax,[buffer];return buffer pointer

	ret

section '.data' writable
  hex_table db '0123456789abcdef'
  buffer db '                ',0

