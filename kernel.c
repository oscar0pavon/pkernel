
#include "boot/library.h"
#include "console.h"
#include "framebuffer.h"

void hang(){
	while(1){};
}

void pkernel(void* in_frame_buffer){
	copy_memory(&frame_buffer, in_frame_buffer, sizeof(struct FrameBuffer));
	clear();
	print("Hello World! by: pkernel");
	hang();	
}
