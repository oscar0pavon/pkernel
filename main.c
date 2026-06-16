
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

byte read_pit_count(void){
	clear_interptions();
	byte count = 0;

	output_byte(0b00000000, 0x43);

	count = input_byte(0x40);
	count |= input_byte(0x40)<<8;
	return count;
}

void run_counter() {
  uint64_t tick = 0;
  u16 seconds = 0;
  for (int i = 0; i < 100000; i++) {
    byte start_counter = read_pit_count();
    byte current_count = 0xFE;
    while (1) {
      current_count = read_pit_count();
      if (start_counter < current_count)
        break;
    }
    tick++;
    if (tick % 5400 == 0) {
      clear_current_line();
      printf("Time: %d", seconds);
      seconds++;
    }
  }
  printf("\n");
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

  lapic_timer_init(100);     // calibrate + start periodic timer at 100 Hz
  task_create("idle", idle_task);

  xhci_enable_msi(0x21);     // configure PCI MSI + enable xHCI interrupter
  asm volatile("sti");   // unmask interrupts — keyboard IRQs can now fire

  printf("--You are in owner space now--\n");
  shell_run();

}
