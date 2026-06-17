
#include "library.h"
#include "console.h"
#include "framebuffer.h"
#include "acpi.h"

#include "input.h"

#include "input_output.h"
#include "pkernel.h"
#include "memory.h"

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

	init_frambuffer(&boot_info->frame_buffer);	

  uint64_t xsdt_address = boot_info->xsdt_address;

  
	printf("pkernel\n");

  init_gdt();
  init_idt();
  init_lapic();
  sched_init();
  set_idt_gate(0x20, (uint64_t)irq_sched_handler);
  set_idt_gate(0x21, (uint64_t)irq_xhci_handler);

  pmm_init(boot_info);

  init_kernel_paging();


	XSDT = (struct XSDT_t*)xsdt_address;
	parse_XSDT();
  
  setup_pci();

  lapic_timer_init(100);     // calibrate + start periodic timer at 100 Hz
  task_create("idle", idle_task);

  xhci_enable_msi(0x21);     // configure PCI MSI + enable xHCI interrupter
  asm volatile("sti");   // unmask interrupts — keyboard IRQs can now fire

  printf("--You are in owner space now--\n");

  task_create("counter", print_every_seconds);

  task_create("shell", shell_run);

  hang();

}
