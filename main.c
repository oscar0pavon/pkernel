
#include "library.h"
#include "console.h"
#include "framebuffer.h"
#include "acpi.h"

#include "input.h"
#include "drivers/pci.h"

#include "input_output.h"


typedef struct MemoryMapInfo{
  uint64_t buffer_address; // Absolute physical memory address of the mmap data (0x5000000)
  uint64_t total_size;     // Total bytes populated in the buffer
  uint64_t descriptor_size;// Unique hardware stepping size (usually 40 or 48 bytes)
}MemoryMapInfo;

typedef struct BootInfo{
  FrameBuffer frame_buffer;
  MemoryMapInfo memory_info;
  uint64_t xsdt_address;
}BootInfo;

void hang(){
	while(1){};
}

byte read_pit_count(void){
	clear_interptions();
	byte count = 0;

	output_byte(0b00000000, 0x43);

	count = input_byte(0x40);
	count |= input_byte(0x40)<<8;
	return count;
}

void main(BootInfo* boot_info){
	init_frambuffer(&boot_info->frame_buffer);	
  uint64_t xsdt_address = boot_info->xsdt_address;

	printf("pkernel\n");


  printf("XSDT address: %x\n",xsdt_address);

	XSDT = (struct XSDT_t*)xsdt_address;
	parse_XSDT();

	printf("parsed acpi\n");
	// printf("PCI List\n");
	// print_pci_list();
	//create_base_address();


	printf("--You are in owner space now--\n");



	uint64_t tick = 0;
	u16 seconds = 0;
	for(int i = 0; i < 100000; i++){
		byte start_counter = read_pit_count();
		byte current_count = 0xFE;
		while(1){
			current_count = read_pit_count();
			if(start_counter<current_count)
				break;
		}
		tick++;
		if(tick%5400 == 0){
			clear_current_line();
			printf("Time: %d",seconds);
			seconds++;
		}
	}
	printf("\n");
	
	printf("PS/2 Virtual driver\n");
	input_loop();

	hang();	
}
