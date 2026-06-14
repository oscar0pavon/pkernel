#include "acpi.h"
#include "console.h"
#include "drivers/pci.h"
#include "drivers/xhci.h"
#include <stdint.h>

struct FADT_t *FADT = NULL;
struct XSDP_t *XSDP = NULL;
struct DSDT_t *DSDT = NULL;
struct XSDT_t *XSDT = NULL;

void acpi_find_FADT() {}

bool acpi_compare_signature(char *signature1, char *signature2) {
  if (signature1[0] != signature2[0])
    return false;
  if (signature1[1] != signature2[1])
    return false;
  if (signature1[2] != signature2[2])
    return false;
  if (signature1[3] != signature2[3])
    return false;
  return true;
}

void parse_FADT() {

  if (acpi_compare_signature(FADT->header.signature, "FACP")) {
    uint32_t fadt_size = FADT->header.length;
    if (fadt_size != sizeof(struct FADT_t)) {
      printf("FADT size not equal %d %d\n", FADT->header.length,
             sizeof(struct FADT_t));
    }

    ACPISystemDescriptorTableHeader *header =
        (ACPISystemDescriptorTableHeader *)(FADT->X_Dsdt);

    header = (ACPISystemDescriptorTableHeader *)&FADT->X_Dsdt;

    if (acpi_compare_signature(header->signature, "DSDT")) {
      printf("Work DSDT\n");
    } else {
      printf("DSDT not work\n");
    }
    if (FADT->X_Dsdt == 0) {
      printf("DSDT memory zero\n");
    }
    DSDT = (struct DSDT_t *)FADT->X_Dsdt;
    // print(DSDT->header.signature);
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
      printf("Found FADT with size %d\n", myheader->length);
      FADT = (struct FADT_t *)myheader;
      parse_FADT();
      continue;
    }
    if (acpi_compare_signature(myheader->signature, "APIC")) {
      printf("Fount MADT with size %d\n", myheader->length);
    }

    // PCI Express Extended Configuration Space (ECAM).
    if (acpi_compare_signature(myheader->signature, "MCFG")) {
      struct MCFG_t *mcfg = (struct MCFG_t *)myheader;
      printf("Found MCFG Table with size %d\n", myheader->length);

      // Pointer to the first entry block, which starts exactly 44 bytes into
      // the table
      uint8_t *table_bytes = (uint8_t *)myheader;
      struct MCFGStructureEntry *first_entry =
          (struct MCFGStructureEntry *)(table_bytes + 44);

      // Read properties from the first entry directly
      //uint64_t* mmio = get_pcie_mmio_address();
      //*mmio = first_entry->BaseAddress;
      pcie_mmio_base_address = first_entry->BaseAddress;

      printf("--> SUCCESS! MCFG Entry 0 Base Address: %x\n",
            pcie_mmio_base_address);
      printf("--> Bus Range: %d to %d\n", first_entry->StartBusNumber,
             first_entry->EndBusNumber);

      setup_pci();
    }
  }
}
