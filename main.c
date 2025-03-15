
#include "library.h"
#include "console.h"
#include "framebuffer.h"
#include "acpi.h"

#include "input.h"
#include "drivers/pci.h"

void hang(){
	while(1){};
}

void main(void* in_frame_buffer,uint64_t acpi_table){
	init_frambuffer(in_frame_buffer);	
	clear();

	printf("pkernel");


	XSDT = (struct XSDT_t*)acpi_table;
	//parse_XDST();

	//print("parsed acpi");
	//print_pci_list();


	printf("--You are in owner space now--\n");

	//printf("number %d",40);
	printf("first\n")	;
	printf("second\n")	;
	printf("second %d and this %d\n",34455,555)	;
	printf("third\n")	;
	printf("hex %x my man\n",0xfeed1);
	printf("test");

	//input_loop();

	hang();	
}
