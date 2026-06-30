#ifndef PKERNEL_H
#define PKERNEL_H

#include "framebuffer.h"
#include "memory.h"

typedef struct BootInfo{
  FrameBuffer frame_buffer;
  MemoryMapInfo memory_info;
  uint64_t xsdt_address;
}BootInfo;

#endif
