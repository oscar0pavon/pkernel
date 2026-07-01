
#include "pkernel.h"

#include "console.h"
#include "acpi.h"

#include "gdt.h"
#include "idt.h"
#include "lapic.h"
#include "paging.h"
#include "usermode.h"
#include "drivers/xhci.h"
#include "drivers/serial.h"
#include "lapic_timer.h"
#include "sched.h"
#include "smp.h"
#include "shell.h"
#include "input_output.h"
#include "cpu.h"

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

void disable_pic(){

  // Mask all legacy 8259 PIC interrupts so they don't fire on our vectors.
  // This is required even when using MSI — a spurious PIC IRQ would hit
  // vector 0x07 or 0x0F and fault since those entries are unhandled.
  output_byte(0xFF, 0xA1);  // slave  PIC: mask all 8 lines
  output_byte(0xFF, 0x21);  // master PIC: mask all 8 lines
}


void main(BootInfo* boot_info){

  serial_init();

	console_init(&boot_info->frame_buffer);	

	printf("========================|| pkernel ||===============================\n");

  setup_acpi(boot_info->xsdt_address);

  init_gdt();

  init_idt();

  disable_pic();

  init_lapic();
  
  pmm_init(boot_info);

  init_paging();

  get_cpus((struct MADT_t*)MADT);
  smp_init();                // INIT-SIPI-SIPI the other CPUs into ap_main()

  lapic_timer_init(100);     // calibrate + start periodic timer at 100 Hz

  sched_init();
  
  set_idt_gate(0x20, (uint64_t)irq_sched_handler);
  set_idt_gate(0x21, (uint64_t)irq_xhci_handler);



  setup_pci();
  xhci_enable_msi(0x21);     // configure PCI MSI + enable xHCI interrupter

  usermode_init();           // TSS + syscall/sysret MSRs for ring 3
  
  task_create("idle", idle_task);

  //task_create("counter", print_every_seconds);

  task_create("shell", shell_run);

  asm volatile("sti");   // unmask interrupts — keyboard IRQs can now fire

  
  printf("--You are in owner space now--\n");

  hang();

}
