#include "shell.h"
#include "input.h"
#include "console.h"
#include "memory.h"
#include "lapic_timer.h"
#include "sched.h"
#include "drivers/pci.h"

#define LINE_MAX 256

static char line[LINE_MAX];
static int  line_len = 0;

static int str_eq(const char *a, const char *b) {
    while (*a && *b) if (*a++ != *b++) return 0;
    return *a == *b;
}

static void cmd_help(void) {
    printf("commands: help  clear  mem  uptime  tasks  lspci\n");
}

static void cmd_lspci(void) {
    static PciDevice devs[MAX_PCI_DEVICES];
    int count = get_pci_list(devs, MAX_PCI_DEVICES);
    for (int i = 0; i < count; i++) {
        PciDevice *d = &devs[i];
        printf("%02x:%02x.%x  %04x:%04x  class %02x:%02x:%02x\n",
               d->bus, d->device, d->function,
               d->vendor_id, d->device_id,
               d->class_code, d->subclass, d->prog_if);
    }
    printf("%d device(s) found\n", count);
}

static void cmd_tasks(void) {
    uint64_t now  = lapic_timer_uptime_ms();
    Task    *head = sched_task_list();
    Task    *t    = head;
    do {
        if (t->wake_time > now)
            printf("  [%d] %-8s  switches=%d  sleeping %d ms\n",
                   t->tid, t->name, (uint32_t)t->switches,
                   (uint32_t)(t->wake_time - now));
        else
            printf("  [%d] %-8s  switches=%d\n",
                   t->tid, t->name, (uint32_t)t->switches);
        t = t->next;
    } while (t != head);
}

static void cmd_uptime(void) {
    uint64_t ms = lapic_timer_uptime_ms();
    printf("%d.%d s (%d ticks)\n",
           (uint32_t)(ms / 1000),
           (uint32_t)(ms % 1000),
           (uint32_t)lapic_timer_get_ticks());
}

static void cmd_mem(void) {
    void *a = kmalloc(64);
    void *b = kmalloc(128);
    void *c = kmalloc(32);
    printf("kmalloc(64)  = 0x%lx\n", (uint64_t)a);
    printf("kmalloc(128) = 0x%lx\n", (uint64_t)b);
    printf("kmalloc(32)  = 0x%lx\n", (uint64_t)c);
    kfree(b);
    void *d = kmalloc(64);
    printf("kfree(b), kmalloc(64) = 0x%lx (reused b)\n", (uint64_t)d);
    kfree(a); kfree(c); kfree(d);
}

static void dispatch(void) {
    if (line_len == 0) return;
    line[line_len] = '\0';

    if      (str_eq(line, "help"))   cmd_help();
    else if (str_eq(line, "clear"))  console_clear();
    else if (str_eq(line, "mem"))    cmd_mem();
    else if (str_eq(line, "uptime")) cmd_uptime();
    else if (str_eq(line, "tasks"))  cmd_tasks();
    else if (str_eq(line, "lspci")) cmd_lspci();
    else    printf("unknown: %s\n", line);
}

static void prompt(void) {
    printf("pkernel> ");
}

void shell_run(void) {
    prompt();
    while (1) {
        while (input_available()) {
            char c = input_getc();
            if (c == '\n') {
                printf("\n");
                dispatch();
                line_len = 0;
                prompt();
            } else if (c == '\b') {
                if (line_len > 0) {
                    line_len--;
                    console_backspace();
                }
            } else if (c >= ' ' && c <= '~' && line_len < LINE_MAX - 1) {
                line[line_len++] = c;
                char s[2] = {c, '\0'};
                printf("%s", s);
            }
        }
        asm volatile("hlt");
    }
}
