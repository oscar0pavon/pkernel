
#include "boot/library.h"
#include "console.h"
#include "framebuffer.h"
#include "acpi.h"

void hang(){
	while(1){};
}

void pkernel(void* in_frame_buffer,uint64_t acpi_table){
	copy_memory(&frame_buffer, in_frame_buffer, sizeof(struct FrameBuffer));
	clear();
	print("Hello World! by: pkernel");

	print("test");

	XSDT = (struct XSDT_t*)acpi_table;
	//parse_XDST();

	print("parsed acpi");

	hang();	
}
