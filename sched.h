#ifndef SCHED_H
#define SCHED_H

#include <stdint.h>

typedef struct Task {
    uint64_t     rsp;      // saved kernel stack pointer (all regs pushed on it)
    uint8_t     *stack;    // base of allocated stack (NULL for the main task)
    uint32_t     tid;
    uint64_t     switches; // how many times this task has been scheduled
    const char  *name;
    struct Task *next;     // circular linked list
} Task;

void    sched_init(void);
Task   *task_create(const char *name, void (*func)(void));
uint64_t sched_tick(uint64_t rsp);  // called from irq_sched_handler; returns new rsp
Task   *sched_task_list(void);      // head of circular task list (main task)

#endif
