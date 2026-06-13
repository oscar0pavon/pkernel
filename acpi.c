#include "acpi.h"
#include <stdint.h>
#include "console.h"

struct FADT_t* FADT = NULL;
struct XSDP_t* XSDP = NULL;
struct DSDT_t* DSDT = NULL;
struct XSDT_t* XSDT = NULL;

uint64_t pcie_mmio_base_address = 0;

void acpi_find_FADT(){

}

bool acpi_compare_signature(char* signature1, char* signature2){
	if(signature1[0] != signature2[0])return false;
	if(signature1[1] != signature2[1])return false;
	if(signature1[2] != signature2[2])return false;
	if(signature1[3] != signature2[3])return false;
	return true;
}

void parse_FADT(){

	if(acpi_compare_signature(FADT->header.signature, "FACP")){
    uint32_t fadt_size = FADT->header.length;
    if(fadt_size != sizeof(struct FADT_t)){
      printf("FADT size not equal %d %d\n", 
          FADT->header.length, sizeof(struct FADT_t));
    }

    ACPISystemDescriptorTableHeader* header = 
      (ACPISystemDescriptorTableHeader*)(FADT->X_Dsdt);

    header = (ACPISystemDescriptorTableHeader*)&FADT->X_Dsdt;

		if(acpi_compare_signature(header->signature, "DSDT")){
			printf("Work DSDT\n");
		}else{
			printf("DSDT not work\n");
		}
		if(FADT->X_Dsdt == 0){
			printf("DSDT memory zero\n");
		}
		DSDT = (struct DSDT_t*)FADT->X_Dsdt;
		//print(DSDT->header.signature);

	}
}

uint64_t xhci_base_mmio = 0;

void find_xhci_via_mcfg() {
  // Use your exact MCFG address variable here
  uint64_t mcfg_base = pcie_mmio_base_address;
  uint8_t bus = 0;

  for (uint8_t dev = 0; dev < 32; dev++) {
    for (uint8_t func = 0; func < 8; func++) {
      // Calculate the 64-bit physical address for this PCI slot's registers
      uint64_t device_config_space = mcfg_base + ((uint64_t)bus << 20) |
                                     ((uint64_t)dev << 15) |
                                     ((uint64_t)func << 12);
      volatile uint32_t *pci_regs = (volatile uint32_t *)device_config_space;

      uint32_t id_reg = pci_regs[0]; // Offset 0x00: Vendor/Device ID
      if (id_reg == 0xFFFFFFFF || id_reg == 0x00000000) {
        if (func == 0)
          break;
        continue;
      }

      uint32_t class_reg = pci_regs[2]; // Offset 0x08: Class/Subclass/ProgIF
      uint8_t class = class_reg >> 24;
      uint8_t sub_class = (class_reg >> 16) & 0xFF;
      uint8_t prog_if = (class_reg >> 8) & 0xFF;

      // Match xHCI USB 3.0 Controller Spec
      if (class == 0x0C && sub_class == 0x03 && prog_if == 0x30) {
        printf("xHCI Device Found at Slot %d, Func %d!\n", dev, func);

        // 1. MANDATORY: Enable Memory Space and Bus Mastering in Command Reg
        // (Offset 0x04)
        pci_regs[1] |= (1 << 1) | (1 << 2);

        // 2. Read BAR0 and BAR1 (Offsets 0x10 and 0x14 / Indices 4 and 5)
        uint32_t bar0 = pci_regs[4];
        uint32_t bar1 = pci_regs[5];

        xhci_base_mmio = (bar0 & 0xFFFFFFF0);
        if ((bar0 & 0x06) == 0x04) {
          xhci_base_mmio |= ((uint64_t)bar1 << 32);
        }

        printf("xHCI Controller Internal Registers live at: %lx\n",
               xhci_base_mmio);
        return;
      }
    }
  }
}

void parse_XSDT() {

  if (acpi_compare_signature(XSDT->header.signature, "XSDT")) {
    printf("is XSDT table\n");
  }

  uint32_t number_of_entries_XSDT =
      (XSDT->header.length - sizeof(ACPISystemDescriptorTableHeader)) / 8;

  for (int i = 0; i < number_of_entries_XSDT; i++) {
    ACPISystemDescriptorTableHeader *myheader =
        (ACPISystemDescriptorTableHeader *)XSDT->entries[i];

    // print(myheader->signature);
    if (acpi_compare_signature(myheader->signature, "FACP")) {
      printf("Found FADT with size %d\n",myheader->length);
      FADT = (struct FADT_t *)myheader;
      parse_FADT();
      continue;
    }
    if (acpi_compare_signature(myheader->signature, "APIC")) {
      printf("Fount MADT with size %d\n", myheader->length);
    }

    //PCI Express Extended Configuration Space (ECAM).
    if (acpi_compare_signature(myheader->signature, "MCFG")) {
      struct MCFG_t *mcfg = (struct MCFG_t *)myheader;
      printf("Found MCFG Table with size %d\n", myheader->length);
      

      // Pointer to the first entry block, which starts exactly 44 bytes into the table
      uint8_t* table_bytes = (uint8_t*)myheader;
      struct MCFGStructureEntry* first_entry = 
        (struct MCFGStructureEntry*)(table_bytes + 44);

      // Read properties from the first entry directly
      pcie_mmio_base_address = first_entry->BaseAddress;
      
      printf("--> SUCCESS! MCFG Entry 0 Base Address: %x\n", pcie_mmio_base_address);
      printf("--> Bus Range: %d to %d\n", 
          first_entry->StartBusNumber, first_entry->EndBusNumber);

      find_xhci_via_mcfg();

    }
  }
}
