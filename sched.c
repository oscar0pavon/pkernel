#include "sched.h"
#include "memory.h"
#include "lapic_timer.h"

// irq_sched_handler pushes 15 registers then the CPU-generated iretq frame.
// In 64-bit mode iretq always pops all five slots regardless of privilege level.
// Stack layout from the r15 slot pointer (low → high):
//   [+0]   r15   [+8]  r14  [+16] r13  [+24] r12  [+32] rbx
//   [+40]  rbp   [+48] r11  [+56] r10  [+64] r9   [+72] r8
//   [+80]  rdi   [+88] rsi  [+96] rdx  [+104] rcx [+112] rax
//   [+120] rip   [+128] cs  [+136] rflags  [+144] rsp  [+152] ss

extern void task_trampoline(void);

#define TASK_STACK_SIZE 4096  // one physical page; allocated via pmm_alloc_page

static Task     main_task;
static Task    *current  = &main_task;
static uint32_t next_tid = 0;

void sched_init(void) {
    asm volatile("mov %%rsp, %0" : "=r"(main_task.rsp));
    main_task.stack     = 0;
    main_task.tid       = next_tid++;
    main_task.switches  = 0;
    main_task.wake_time = 0;
    main_task.name      = "main";
    main_task.next      = &main_task;
    current = &main_task;
}

Task *task_create(const char *name, void (*func)(void)) {
    Task    *task = kmalloc(sizeof(Task));
    uint8_t *stack = (uint8_t *)pmm_alloc_page();  // 4 KB, page-aligned, always fits

    // Build initial register frame from the top of the stack downward.
    // iretq frame: five slots (64-bit always pops all five)
    uint64_t *stack_pointer = (uint64_t *)(stack + TASK_STACK_SIZE);
    *--stack_pointer = 0x10ULL;                         // ss
    *--stack_pointer = (uint64_t)(stack + TASK_STACK_SIZE); // rsp: task's own stack top
    *--stack_pointer = 0x202ULL;                        // rflags: IF=1
    *--stack_pointer = 0x08ULL;                         // cs
    *--stack_pointer = (uint64_t)task_trampoline;       // rip: trampoline calls r15 then task_exit
    
    
    // saved registers: r15 carries the real entry point, rest zeroed
    for (int i = 0; i < 14; i++){
      *--stack_pointer = 0; // rax .. r14
    }

    *--stack_pointer = (uint64_t)func;                  // r15


    task->rsp       = (uint64_t)stack_pointer;
    task->stack     = stack;
    task->tid       = next_tid++;
    task->switches  = 0;
    task->wake_time = 0;
    task->name      = name;
    

    // Insert into circular list right after current task
    task->next       = current->next;
    current->next = task;

    return task;
}

// Removes current from the circular list and returns the next task's rsp.
// Called from task_exit in sched_asm.s with interrupts disabled.
uint64_t task_remove_current(void) {
    Task *dying = current;
    Task *prev  = dying->next;
    while (prev->next != dying) prev = prev->next;
    prev->next = dying->next;

    current = dying->next;
    current->switches++;
    return current->rsp;
}

// Called from irq_sched_handler with the current task's saved-register rsp.
// Returns the rsp to switch to (next task's saved-register frame).
uint64_t sched_tick(uint64_t rsp) {
    lapic_timer_isr();      // ticks++ and LAPIC EOI

    current->rsp = rsp;

    uint64_t now  = lapic_timer_uptime_ms();
    Task    *next = current->next;
    Task    *stop = next;           // remember where we started scanning
    while (next->wake_time > now) {
        next = next->next;
        if (next == stop) { next = current; break; }
    }
    if (next != current) {
        current = next;
        current->switches++;
    }
    return current->rsp;
}

void task_sleep(uint64_t ms) {
    current->wake_time = lapic_timer_uptime_ms() + ms;
    while (lapic_timer_uptime_ms() < current->wake_time)
        asm volatile("hlt");        // yield each tick; sched_tick skips us while sleeping
    current->wake_time = 0;
}

Task *sched_task_list(void) {
    return &main_task;
}
