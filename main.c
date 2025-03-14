
#include "library.h"
#include "console.h"
#include "framebuffer.h"
#include "acpi.h"

#include "input_output.h"
#include <stdint.h>

void hang(){
	while(1){};
}

void main(void* in_frame_buffer,uint64_t acpi_table){
	init_frambuffer(in_frame_buffer);	
	clear();
	print("Hello World! by: pkernel");

	print("test");

	byte data = 0; 
	data = port_60();
	print_byte_hex(data);

	XSDT = (struct XSDT_t*)acpi_table;
	//parse_XDST();

	print("parsed acpi");

	hang();	
}
