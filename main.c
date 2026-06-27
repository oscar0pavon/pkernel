
#include "pkernel.h"

#include "console.h"
#include "acpi.h"

#include "gdt.h"
#include "idt.h"
#include "lapic.h"
#include "paging.h"
#include "drivers/xhci.h"
#include "drivers/serial.h"
#include "lapic_timer.h"
#include "sched.h"
#include "shell.h"

void hang(void) {
  while (1) { asm volatile("hlt"); }
}

static void idle_task(void) {
  while (1) { asm volatile("hlt"); }
}

void print_every_seconds(void){
  for (int i = 0; i < 10; i++) {
    printf("Seconds %d\n", i);
    task_sleep(1000);
  }
}


void main(BootInfo* boot_info){

  serial_init();

	console_init(&boot_info->frame_buffer);	

	printf("========================|| pkernel ||===============================\n");

  setup_acpi(boot_info->xsdt_address);

  init_gdt();
  init_idt();
  
  pmm_init(boot_info);

  init_paging();

  init_lapic();
  lapic_timer_init(100);     // calibrate + start periodic timer at 100 Hz

  sched_init();
  
  set_idt_gate(0x20, (uint64_t)irq_sched_handler);
  set_idt_gate(0x21, (uint64_t)irq_xhci_handler);

  
  setup_pci();
  xhci_enable_msi(0x21);     // configure PCI MSI + enable xHCI interrupter

  task_create("idle", idle_task);

  task_create("counter", print_every_seconds);

  task_create("shell", shell_run);

  asm volatile("sti");   // unmask interrupts — keyboard IRQs can now fire

  
  printf("--You are in owner space now--\n");

  hang();

}
