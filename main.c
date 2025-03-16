
#include "library.h"
#include "console.h"
#include "framebuffer.h"
#include "acpi.h"

#include "input.h"
#include "drivers/pci.h"

#include "input_output.h"

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

void main(void* in_frame_buffer,uint64_t acpi_table){
	init_frambuffer(in_frame_buffer);	
	clear();

	printf("pkernel\n");


	XSDT = (struct XSDT_t*)acpi_table;
	//parse_XDST();

	//print("parsed acpi");
	print_pci_list();


	printf("--You are in owner space now--\n");



	uint64_t tick = 0;
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
			printf("one second\n");
		}
	}

	input_loop();

	hang();	
}
