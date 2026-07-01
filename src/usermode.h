#ifndef USERMODE_H
#define USERMODE_H

#include <stdint.h>

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
