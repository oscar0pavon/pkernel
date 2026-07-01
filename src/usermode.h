#ifndef USERMODE_H
#define USERMODE_H

#include <stdint.h>

// --- our tiny syscall ABI --------------------------------------------------
// rax = number, args in rdi/rsi/rdx (Linux-style; rcx/r11 are eaten by the
// `syscall` instruction itself), return value in rax.
#define SYS_EXIT 0     // rdi = exit code   -> never returns to user
#define SYS_PUTCHAR 1  // rdi = character   -> prints one char

// --- syscall/sysret configuration MSRs -------------------------------------
#define IA32_EFER 0xC0000080
#define IA32_STAR 0xC0000081
#define IA32_LSTAR 0xC0000082
#define IA32_FMASK 0xC0000084
#define EFER_SCE (1ULL << 0)  // System Call Extensions: enables syscall/sysret

// Dedicated ring-3 virtual window. 0x40000000000 lives under PML4 slot 8, which
// the kernel identity map (slot 0, low 4 GB) never touches — so marking these
// pages user-accessible cannot expose any kernel memory.
#define USER_CODE_VA 0x40000000000ULL
#define USER_STACK_VA 0x40000002000ULL  // separate page; its top is the initial rsp

// 64-bit Task State Segment. In long mode the CPU only cares about rsp0/rsp1/
// rsp2 and the IST array; for us only rsp0 matters — it's the stack loaded when
// an interrupt fires while the CPU is in ring 3.
typedef struct {
  uint32_t reserved0;
  uint64_t rsp0;
  uint64_t rsp1;
  uint64_t rsp2;
  uint64_t reserved1;
  uint64_t ist[7];
  uint64_t reserved2;
  uint16_t reserved3;
  uint16_t iomap_base;
} __attribute__((packed)) TaskStateSegment;

// Provided by usermode.s.
extern uint64_t syscall_kernel_rsp;
extern void syscall_entry(void);
extern void return_to_kernel(uint64_t code);
extern uint64_t enter_user_mode(uint64_t entry, uint64_t user_stack_top);

// The ring-3 payload, assembled in user_program.s (position-independent bytes).
extern uint8_t user_program_start[];
extern uint8_t user_program_end[];


// Set up ring-3 support: fill the TSS (interrupt stack for ring3->ring0), and
// program the syscall/sysret MSRs (EFER.SCE, STAR, LSTAR, FMASK). Call once,
// after the GDT and paging are up.
void usermode_init(void);

// Copy the bundled demo program into a fresh user page, map a user stack, and
// run it in ring 3. Returns after the program calls SYS_EXIT.
void run_user_program(void);

// syscall dispatcher (C side of syscall_entry in usermode.s).
uint64_t syscall_dispatch(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3);

#endif
