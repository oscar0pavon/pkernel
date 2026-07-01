#include "usermode.h"

#include "console.h"
#include "gdt.h"
#include "library.h"
#include "memory.h"
#include "paging.h"

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
} __attribute__((packed)) Tss;

static Tss tss;

// Two separate kernel stacks: one the CPU switches to on an IRQ from ring 3
// (via TSS.rsp0), one the syscall handler switches to by hand (`syscall` does
// not change rsp itself).
aligned64 static uint8_t irq_stack[16384];
aligned64 static uint8_t syscall_stack[16384];

// Provided by usermode.s.
extern uint64_t syscall_kernel_rsp;
extern void syscall_entry(void);
extern void return_to_kernel(uint64_t code);
extern uint64_t enter_user_mode(uint64_t entry, uint64_t user_stack_top);

// The ring-3 payload, assembled in user_program.s (position-independent bytes).
extern uint8_t user_program_start[];
extern uint8_t user_program_end[];

void usermode_init(void) {
  // TSS: rsp0 is the kernel stack the CPU loads on a ring3 -> ring0 interrupt.
  memset(&tss, 0, sizeof(tss));
  tss.rsp0 = (uint64_t)(irq_stack + sizeof(irq_stack));
  tss.iomap_base = sizeof(tss);  // == limit+1: no I/O permission bitmap
  gdt_install_tss((uint64_t)&tss, sizeof(tss) - 1);

  // Stack the syscall handler switches onto (syscall keeps the user rsp).
  syscall_kernel_rsp = (uint64_t)(syscall_stack + sizeof(syscall_stack));

  // Turn on the syscall/sysret instruction pair.
  write_msr(IA32_EFER, read_msr(IA32_EFER) | EFER_SCE);

  // STAR: on `syscall`, CS=0x08 / SS=0x10 (kernel). On `sysret`, the base 0x10
  // yields user SS=0x18|3 and CS=0x20|3 — which is why the GDT lists user data
  // before user code.
  write_msr(IA32_STAR, (0x0008ULL << 32) | (0x0010ULL << 48));
  write_msr(IA32_LSTAR, (uint64_t)syscall_entry);  // where `syscall` lands
  write_msr(IA32_FMASK, 0x700);  // clear TF, IF, DF on kernel entry

  printf("User mode ready (syscall/sysret)\n");
}

uint64_t syscall_dispatch(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3) {
  (void)a2;
  (void)a3;
  switch (num) {
  case SYS_PUTCHAR: {
    char s[2] = {(char)a1, '\0'};
    printf("%s", s);
    return 0;
  }
  case SYS_EXIT:
    return_to_kernel(a1);  // never returns
    return 0;              // unreachable
  default:
    printf("[syscall] unknown #%lu\n", num);
    return (uint64_t)-1;
  }
}

void run_user_program(void) {
  // Fresh physical pages for the program's code and its stack.
  uint64_t code_phys = (uint64_t)pmm_alloc_page();
  uint64_t stack_phys = (uint64_t)pmm_alloc_page();

  // Copy the position-independent payload into the code page and map both
  // pages into the ring-3 window.
  uint64_t len = (uint64_t)(user_program_end - user_program_start);
  memcpy((void *)code_phys, user_program_start, len);

  paging_map_user_page(USER_CODE_VA, code_phys);
  paging_map_user_page(USER_STACK_VA, stack_phys);

  uint64_t entry = USER_CODE_VA;
  uint64_t user_rsp = USER_STACK_VA + 4096;  // top of the 4 KB stack page

  printf("[user] entering ring 3 at 0x%lx (%lu bytes)\n", entry, len);
  uint64_t code = enter_user_mode(entry, user_rsp);
  printf("[user] back in ring 0, exit code = %lu\n", code);
}
