#include "memory.h"
#include "pkernel.h"
#include "console.h"

void setup_memory(void* info) {
  BootInfo* boot_info = (BootInfo*)info;

  printf("Memory base %lx\n",boot_info->memory_info.buffer_address);

  uint8_t *map_bytes = (uint8_t *)boot_info->memory_info.buffer_address;
  uint64_t total_size = boot_info->memory_info.total_size;
  uint64_t desc_size = boot_info->memory_info.descriptor_size;

  uint64_t entry_count = total_size / desc_size;
  uint64_t usable_ram_bytes = 0;

  for (uint64_t i = 0; i < entry_count; i++) {
    // Step forward precisely by the hardware descriptor stride length!
    struct MemoryDescriptor *desc =
        (struct MemoryDescriptor *)(map_bytes + (i * desc_size));

    // Type 7 == EfiConventionalMemory (This is your free, usable system RAM!)
    if (desc->type == 7) {
      usable_ram_bytes += (desc->pages * 4096);

      // Print memory ranges to verify your structs align beautifully
      printf("Free RAM Segment Found: Start: %lx | Pages: %d\n",
             desc->physical_start, (uint32_t)desc->pages);
    }
  }

  printf("Total Usable Bare-Metal System RAM: %d MB\n",
         (uint32_t)(usable_ram_bytes / 1024 / 1024));
}
