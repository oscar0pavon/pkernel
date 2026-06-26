#include "acpi.h"
#include "console.h"
#include "drivers/pci.h"
#include "drivers/xhci.h"
#include <stdint.h>

struct FADT_t *FADT = NULL;
struct XSDP_t *XSDP = NULL;
struct DSDT_t *DSDT = NULL;
struct XSDT_t *XSDT = NULL;

int cpu_count = 0;
uint8_t cpu_apic_ids[MAX_CPUS];

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
      // printf("FADT size not equal %d %d\n", FADT->header.length,
      //        sizeof(struct FADT_t));
    }

    ACPISystemDescriptorTableHeader *header =
        (ACPISystemDescriptorTableHeader *)(FADT->X_Dsdt);

    header = (ACPISystemDescriptorTableHeader *)&FADT->X_Dsdt;

    DSDT = (struct DSDT_t *)FADT->X_Dsdt;
    // print(DSDT->header.signature);
  }
}

void parse_MADT(struct MADT *madt) {
  uint8_t *p = (uint8_t *)madt + 44; // skip fixed MADT header (36 + 4 + 4)
  uint8_t *end = (uint8_t *)madt + madt->header.length;

  while (p < end) {
    uint8_t type = p[0];
    uint8_t len  = p[1];

    if (type == 0) { // Processor Local APIC
      struct ProcessorLocalAPIC *lapic = (struct ProcessorLocalAPIC *)p;
      if (lapic->flags & 1) { // bit 0: processor enabled
        if (cpu_count < MAX_CPUS)
          cpu_apic_ids[cpu_count++] = lapic->APIC_ID;
      }
    }

    p += len;
  }

  printf("CPUs detected: %d\n", cpu_count);
  for (int i = 0; i < cpu_count; i++)
    printf("  CPU %d: APIC ID %d\n", i, cpu_apic_ids[i]);
}

void parse_XSDT() {

  // if (acpi_compare_signature(XSDT->header.signature, "XSDT")) {
  //   printf("is XSDT table\n");
  // }

  uint32_t number_of_entries_XSDT =
      (XSDT->header.length - sizeof(ACPISystemDescriptorTableHeader)) / 8;

  for (int i = 0; i < number_of_entries_XSDT; i++) {
    ACPISystemDescriptorTableHeader *myheader =
        (ACPISystemDescriptorTableHeader *)XSDT->entries[i];

    // print(myheader->signature);
    if (acpi_compare_signature(myheader->signature, "FACP")) {
      // printf("Found FADT with size %d\n", myheader->length);
      FADT = (struct FADT_t *)myheader;
      parse_FADT();
      continue;
    }
    if (acpi_compare_signature(myheader->signature, "APIC")) {
      parse_MADT((struct MADT *)myheader);
    }

    // PCI Express Extended Configuration Space (ECAM).
    if (acpi_compare_signature(myheader->signature, "MCFG")) {
      struct MCFG_t *mcfg = (struct MCFG_t *)myheader;
      // Pointer to the first entry block, which starts exactly 44 bytes into
      // the table
      uint8_t *table_bytes = (uint8_t *)myheader;
      struct MCFGStructureEntry *first_entry =
          (struct MCFGStructureEntry *)(table_bytes + 44);

      pcie_mmio_base_address = first_entry->BaseAddress;

    }
  }
}

void setup_acpi(uint64_t xsdt_address){
	XSDT = (struct XSDT_t*)xsdt_address;
	parse_XSDT();
}
