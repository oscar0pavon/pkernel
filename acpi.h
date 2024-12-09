
//Advancded Configuration and Power Interface (ACPI)

//eXtended System Descriptor Pointer (XSDP)

#ifndef _ACPI_H_
#define _ACPI_H_

#include "boot/types.h"

struct XSDP_t {
	char signature[8];
	uint8_t checksum;
	char OEMID[6];
	uint8_t revision;
	uint32_t rsdt_address;
	uint32_t length;
	uint64_t XSDT_address;//XSDT(eXtended System Description Table)
	uint8_t extended_checksum;
	uint8_t reserved[3];
}__attribute__ ((packed));




struct ACPISystemDescriptorTableHeader{
	char signature[4];
	uint32_t length;
	uint8_t revision;
	uint8_t checksum;
	char OEMID[6];
	char OEMTableID[8];
	uint32_t OPEMRevision;
	uint32_t creator_id;
	uint32_t creator_revision;
}__attribute__ ((packed));


struct __attribute__((packed, aligned(4))) XSDT_t {
  struct ACPISystemDescriptorTableHeader header;
  uint64_t entries[];
};

//Differentiated System Description Table Fields (DSDT)
struct __attribute__((packed, aligned(4))) DSDT_t {
  struct ACPISystemDescriptorTableHeader header;
  uint64_t entries[];
};

typedef struct {
	uint8_t address_space;
	uint8_t bit_width;
	uint8_t bit_offset;
	uint8_t access_size;
	uint64_t address;
}GenericAddressStructure;

//Fixed ACPI Desciption Table (FADT)
struct FADT_t
{
    struct   ACPISystemDescriptorTableHeader header;
    uint32_t FirmwareCtrl;
    uint32_t Dsdt;

    // field used in ACPI 1.0; no longer in use, for compatibility only
    uint8_t  Reserved;

    uint8_t  PreferredPowerManagementProfile;
    uint16_t SCI_Interrupt;
    uint32_t SMI_CommandPort;
    uint8_t  AcpiEnable;
    uint8_t  AcpiDisable;
    uint8_t  S4BIOS_REQ;
    uint8_t  PSTATE_Control;

    uint32_t PM1aEventBlock;
    uint32_t PM1bEventBlock;
    uint32_t PM1aControlBlock;
    uint32_t PM1bControlBlock;
    uint32_t PM2ControlBlock;
    uint32_t PMTimerBlock;
    uint32_t GPE0Block;
    uint32_t GPE1Block;

    uint8_t  PM1EventLength;
    uint8_t  PM1ControlLength;
    uint8_t  PM2ControlLength;
    uint8_t  PMTimerLength;
    uint8_t  GPE0Length;
    uint8_t  GPE1Length;
    uint8_t  GPE1Base;
    uint8_t  CStateControl;

    uint16_t WorstC2Latency;
    uint16_t WorstC3Latency;
    uint16_t FlushSize;
    uint16_t FlushStride;

    uint8_t  DutyOffset;
    uint8_t  DutyWidth;
    uint8_t  DayAlarm;
    uint8_t  MonthAlarm;
    uint8_t  Century;

    // reserved in ACPI 1.0; used since ACPI 2.0+
    uint16_t BootArchitectureFlags;

    uint8_t  Reserved2;
    uint32_t Flags;

    // 12 byte structure; see below for details
    GenericAddressStructure ResetReg;

    uint8_t  ResetValue;
    uint8_t  Reserved3[3];
  
    // 64bit pointers - Available on ACPI 2.0+
    uint64_t                X_FirmwareControl;
    uint64_t                X_Dsdt;

    GenericAddressStructure X_PM1aEventBlock;
    GenericAddressStructure X_PM1bEventBlock;
    GenericAddressStructure X_PM1aControlBlock;
    GenericAddressStructure X_PM1bControlBlock;
    GenericAddressStructure X_PM2ControlBlock;
    GenericAddressStructure X_PMTimerBlock;
    GenericAddressStructure X_GPE0Block;
    GenericAddressStructure X_GPE1Block;
};


//APIC ("Advanced Programmable Interrupt Controller")
//Multiple APIC Description Table (MADT) 
struct MADT{
    struct ACPISystemDescriptorTableHeader header;
    uint32_t local_interrupt_controller_address;
    uint32_t flags;
    void* interrupt_controller_structures[];
};

struct ProcessorLocalAPIC{
    uint8_t type;
    uint8_t length;//8 bytes
    uint8_t ACPIProcessorUID;
    uint8_t APIC_ID;
    uint16_t flags;
};

struct IOAPIC{
    uint8_t type;
    uint8_t length;//12bytes
    uint8_t io_apic_id;
    uint8_t reserved;
    uint32_t io_apic_address;
    uint32_t global_system_interrupt_base;
};
//Interrupt Source Override Structure 10bytes
//Non-Maskable Interrupt (NMI) Source Structure 8bytes
// Local APIC NMI Structure 6 bytes

struct LocalAPICAddressOverride{
    uint8_t type;
    uint8_t length;
    uint16_t reserved;
    uint64_t local_APIC_address;
};

extern struct FADT_t* FADT;

extern struct XSDP_t* XSDP;

extern struct DSDT_t* DSDT;

extern struct XSDT_t* XSDT;

bool acpi_compare_signature(char* signature1, char* signature2);

void parse_XDST();

#endif
