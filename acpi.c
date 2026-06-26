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

PowerManager power_manager;

void acpi_find_FADT() {}

// Decode one AML integer operand: ZeroOp(0x00)=0, OnesOp(0xFF)=0xFF,
// ByteConst(0x0A XX)=XX. Returns the value; advances *pp past the operand.
static uint8_t aml_read_byte(uint8_t **pp) {
    uint8_t op = *(*pp)++;
    if (op == 0x0A) return *(*pp)++;   // ByteConst
    if (op == 0xFF) return 0xFF;        // OnesOp
    return 0;                           // ZeroOp (0x00) or anything else → 0
}

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

void acpi_get_poweroff(){
  // Scan DSDT AML for "_S5_" and decode SLP_TYPa from its Package value.
  // QEMU typically encodes it as ZeroOp (0x00); real hardware may use ByteConst.
  uint8_t slp_typa = 0;
  if (DSDT && DSDT->header.length > 36) {
      uint8_t *p   = (uint8_t *)DSDT;
      uint8_t *end = p + DSDT->header.length - 9;
      for (; p < end; p++) {
          // Match: NameOp "_S5_" PackageOp
          if (p[0] == 0x08 &&
              p[1] == '_' && p[2] == 'S' && p[3] == '5' && p[4] == '_' &&
              p[5] == 0x12) {
              // p[6] = PkgLength, p[7] = NumElements, p[8..] = elements
              uint8_t *elem = p + 8;
              slp_typa = aml_read_byte(&elem);
              break;
          }
      }
  }

  if (FADT && FADT->PM1aControlBlock) {
    power_manager.poweroff =
        ((uint32_t)slp_typa << 10) | (1u << 13); // SLP_TYP | SLP_EN
  }

}

void parse_fadt() {

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

void parse_madt(struct MADT *madt) {
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

void parse_xsdt() {

  uint32_t number_of_entries_XSDT =
      (XSDT->header.length - sizeof(ACPISystemDescriptorTableHeader)) / 8;

  for (int i = 0; i < number_of_entries_XSDT; i++) {
    ACPISystemDescriptorTableHeader *myheader =
        (ACPISystemDescriptorTableHeader *)XSDT->entries[i];

    if (acpi_compare_signature(myheader->signature, "FACP")) {
      FADT = (struct FADT_t *)myheader;
      parse_fadt();
      continue;
    }
    if (acpi_compare_signature(myheader->signature, "APIC")) {
      parse_madt((struct MADT *)myheader);
    }

    // PCI Express Extended Configuration Space (ECAM).
    if (acpi_compare_signature(myheader->signature, "MCFG")) {
      struct MCFG_t *mcfg = (struct MCFG_t *)myheader;
      // Pointer to the first entry block, 
      // which starts exactly 44 bytes into the table
      uint8_t *table_bytes = (uint8_t *)myheader;
      struct MCFGStructureEntry *first_entry =
          (struct MCFGStructureEntry *)(table_bytes + 44);

      pcie_mmio_base_address = first_entry->BaseAddress;
    }
  }
}

void setup_acpi(uint64_t xsdt_address){
	XSDT = (struct XSDT_t*)xsdt_address;
	parse_xsdt();

  acpi_get_poweroff();
  
}
