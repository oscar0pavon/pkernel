#include "sched.h"
#include "memory.h"
#include "lapic_timer.h"

// irq_sched_handler pushes 15 registers then iretq frame (rip, cs, rflags).
// The frame pointer passed to sched_tick points to the r15 slot (lowest addr).
// Stack layout from that pointer (low → high):
//   [+0]   r15   [+8]  r14  [+16] r13  [+24] r12  [+32] rbx
//   [+40]  rbp   [+48] r11  [+56] r10  [+64] r9   [+72] r8
//   [+80]  rdi   [+88] rsi  [+96] rdx  [+104] rcx [+112] rax
//   [+120] rip   [+128] cs  [+136] rflags

#define TASK_STACK_SIZE 4096  // one physical page; allocated via pmm_alloc_page

static Task     main_task;
static Task    *current  = &main_task;
static uint32_t next_tid = 0;

void sched_init(void) {
    main_task.rsp      = 0;
    main_task.stack    = 0;
    main_task.tid      = next_tid++;
    main_task.switches = 0;
    main_task.name     = "main";
    main_task.next     = &main_task;
    current = &main_task;
}

Task *task_create(const char *name, void (*func)(void)) {
    Task    *t = kmalloc(sizeof(Task));
    uint8_t *s = (uint8_t *)pmm_alloc_page();  // 4 KB, page-aligned, always fits

    // Build initial register frame from the top of the stack downward.
    // When irq_sched_handler restores this task, it pops 15 registers then
    // iretq into func.  In long mode iretq ALWAYS pops all five frame slots
    // (rip, cs, rflags, rsp, ss) — even with no privilege change — so all five
    // must be present or iretq faults loading a bogus ss.
    uint64_t *sp = (uint64_t *)(s + TASK_STACK_SIZE);
    *--sp = 0x10ULL;                          // ss:  kernel data selector
    *--sp = (uint64_t)(s + TASK_STACK_SIZE);  // rsp: task's own stack top
    *--sp = 0x202ULL;                         // rflags: IF=1
    *--sp = 0x08ULL;                          // cs:  kernel code selector
    *--sp = (uint64_t)func;                   // rip: task entry point
    // 15 saved registers (all zero; value order matches push sequence in handler)
    for (int i = 0; i < 15; i++) *--sp = 0;


    t->rsp      = (uint64_t)sp;
    t->stack    = s;
    t->tid      = next_tid++;
    t->switches = 0;
    t->name     = name;
    

    // Insert into circular list right after current task
    t->next       = current->next;
    current->next = t;

    return t;
}

// Called from irq_sched_handler with the current task's saved-register rsp.
// Returns the rsp to switch to (next task's saved-register frame).
uint64_t sched_tick(uint64_t rsp) {
    lapic_timer_isr();      // ticks++ and LAPIC EOI

    current->rsp = rsp;
    current      = current->next;
    current->switches++;

    return current->rsp;
}

Task *sched_task_list(void) {
    return &main_task;
}
